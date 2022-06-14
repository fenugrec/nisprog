/*
 *	nisprog - Nissan ECU communications utility
 *
 * (c) 2022 rimwall
 *
 * Licensed under GPLv3
 *
 * back-end functions for Subaru SSM ECUs
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stypes.h"

#include "diag.h"
#include "diag_l2.h"
#include "diag_iso14230.h"	//for NRC decoding

#include "nisprog.h"
#include "ssm_backend.h"
#include "nissutils/cli_utils/nislib.h"


#define CURFILE "ssm_backend.c"	//HAAAX


/** Decode negative response code into a short error string.
 *
 * rxdata[] must contain at least 3 bytes, "7F <SID> <NRC>"
 * returns a static char * that must not be free'd !
 */
static const char *decode_nrc(uint8_t *rxdata);

static void sub_genkey(const uint8_t *seed8, uint8_t *key);


/*
 * For Subaru, get the ECU ID via SSM A8 command.
 *
 * @return 0 if successful
 */
int sub_get_ecuid(u8 *dest) {
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq

	int errval;

	if (npstate == NP_DISC) {
		printf("Not connected to ECU\nTry \"spconn\" first\n");
		return -1;
	}

	nisreq.data=txdata;	//super very essential !

	//SSM command for ssm_get ECU ID
	txdata[0]=0xA8;  //0xA8 command
	txdata[1]=0x00;  //single response
	txdata[2]=0x00;  //ECU ID Byte 1 trigger ssm_get command at offset per next 2 bytes
	txdata[3]=0x00;  //offset byte 0
	txdata[4]=0x01;  //offset byte 1
	txdata[5]=0x00;  //ECU ID Byte 2 trigger ssm_get command at offset per next 2 bytes
	txdata[6]=0x00;  //offset byte 0
	txdata[7]=0x02;  //offset byte 2
	txdata[8]=0x00;  //ECU ID Byte 3 trigger ssm_get command at offset per next 2 bytes
	txdata[9]=0x00;  //offset byte 0
	txdata[10]=0x03;  //offset byte 3
	txdata[11]=0x00;  //ECU ID Byte 4 trigger ssm_get command at offset per next 2 bytes
	txdata[12]=0x00;  //offset byte 0
	txdata[13]=0x04;  //offset byte 4
	txdata[14]=0x00;  //ECU ID Byte 5 trigger ssm_get command at offset per next 2 bytes
	txdata[15]=0x00;  //offset byte 0
	txdata[16]=0x05;  //offset byte 5
	nisreq.len=17;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len != 6) || (rxmsg->data[0] != 0xE8)) {
		printf("got bad ECU ID response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	memcpy(dest, rxmsg->data + 1, 5);	//skip 0xE8
	dest[5]=0;	//null-terminate

	diag_freemsg(rxmsg);
	return 0;
}


/*
 * For Subaru, use SID 0x81 to start a communications session to access other SID commands.
 *
 * @return 0 if successful
 */
int sub_sid81_startcomms() {
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq

	int errval;

	if (npstate == NP_DISC) {
		printf("Not connected to ECU\nTry \"spconn\" first\n");
	}

	nisreq.data=txdata;	//super very essential !

	txdata[0]=0x81;  //SID 0x81 startCommunications command
	nisreq.len=1;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len != 3) || (rxmsg->data[0] != 0xC1)) {
		printf("got bad SID 0x81 startCommunications response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}

	diag_freemsg(rxmsg);
	return 0;
}


/*
 * For Subaru, use SID 0x27 for security access, step 1 and 2.
 *
 * @return 0 if successful
 */
int sub_sid27_unlock(){
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x27;
	txdata[1]=0x01;	//RequestSeed
	nisreq.len=2;
	nisreq.data=txdata;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len != 6) || (rxmsg->data[0] != 0x67)) {
		printf("got bad 27 01 response : ");
		if (rxmsg->data[0] == 0x7F) {
			printf("%s\n", decode_nrc(rxmsg->data));
		} else {
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
		}
		diag_freemsg(rxmsg);
		return -1;
	}

	printf("\nSeed obtained: %02x %02x %02x %02x\n", rxmsg->data[2], rxmsg->data[3], rxmsg->data[4], rxmsg->data[5]);

	txdata[0]=0x27;
	txdata[1]=0x02;	//SendKey

	sub_genkey(&rxmsg->data[2], &txdata[2]);	//write key to txdata buffer
	printf("Key generated: %02x %02x %02x %02x\n", txdata[2], txdata[3], txdata[4], txdata[5]);

	diag_freemsg(rxmsg);

	printf("\n");

	nisreq.len=6; //27 02 K K K K
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if (rxmsg->data[0] != 0x67) {
		printf("got bad 27 02 response : ");
		if (rxmsg->data[0] == 0x7F) {
			printf("%s\n", decode_nrc(rxmsg->data));
		} else {
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
		}
		diag_freemsg(rxmsg);
		return -1;
	}

	diag_freemsg(rxmsg);
	return 0;
}


/** For Subaru, generates key from seed
 * writes 4 bytes in buffer *key
 */
static void sub_genkey(const uint8_t *seed8, uint8_t *key) {
	uint32_t seed, index;
	uint16_t wordtogenerateindex, wordtobeencrypted, encryptionkey;
	int ki, n;

	const uint16_t keytogenerateindex[]={
		0x53DA, 0x33BC, 0x72EB, 0x437D,
		0x7CA3, 0x3382, 0x834F, 0x3608,
		0xAFB8, 0x503D, 0xDBA3, 0x9D34,
		0x3563, 0x6B70, 0x6E74, 0x88F0};

	const uint8_t indextransformation[]={
		0x5, 0x6, 0x7, 0x1, 0x9, 0xC, 0xD, 0x8,
		0xA, 0xD, 0x2, 0xB, 0xF, 0x4, 0x0, 0x3,
		0xB, 0x4, 0x6, 0x0, 0xF, 0x2, 0xD, 0x9,
		0x5, 0xC, 0x1, 0xA, 0x3, 0xD, 0xE, 0x8};

	seed = reconst_32(seed8);

	for (ki = 15; ki >= 0; ki--) {

		wordtogenerateindex = seed;
		wordtobeencrypted = seed >> 16;	
		index = wordtogenerateindex ^ keytogenerateindex[ki];
		index += index << 16;
		encryptionkey = 0;

		for (n = 0; n < 4; n++) {
			encryptionkey += indextransformation[(index >> (n * 4)) & 0x1F] << (n * 4);	
		}

		encryptionkey = (encryptionkey >> 3) + (encryptionkey << 13);
		seed = (encryptionkey ^ wordtobeencrypted) + (wordtogenerateindex << 16);
	}

	seed = (seed >> 16) + (seed << 16);	
	write_32b(seed, key);

	return;
}


/** For Subaru, encrypts data for upload
 * writes 4 bytes in buffer *encrypteddata
 */
void sub_encrypt(const uint8_t *datatoencrypt, uint8_t *encrypteddata) {
	uint32_t datatoencrypt32, index;
	uint16_t wordtogenerateindex, wordtobeencrypted, encryptionkey;
	int ki, n;

	const uint16_t keytogenerateindex[]={
		0x7856, 0xCE22, 0xF513, 0x6E86};

	const uint8_t indextransformation[]={
		0x5, 0x6, 0x7, 0x1, 0x9, 0xC, 0xD, 0x8,
		0xA, 0xD, 0x2, 0xB, 0xF, 0x4, 0x0, 0x3,
		0xB, 0x4, 0x6, 0x0, 0xF, 0x2, 0xD, 0x9,
		0x5, 0xC, 0x1, 0xA, 0x3, 0xD, 0xE, 0x8};

	datatoencrypt32 = reconst_32(datatoencrypt);

	for (ki = 0; ki < 4; ki++) {

		wordtogenerateindex = datatoencrypt32;
		wordtobeencrypted = datatoencrypt32 >> 16;	
		index = wordtogenerateindex ^ keytogenerateindex[ki];
		index += index << 16;
		encryptionkey = 0;

		for (n = 0; n < 4; n++) {
			encryptionkey += indextransformation[(index >> (n * 4)) & 0x1F] << (n * 4);	
		}

		encryptionkey = (encryptionkey >> 3) + (encryptionkey << 13);
		datatoencrypt32 = (encryptionkey ^ wordtobeencrypted) + (wordtogenerateindex << 16);
	}

	datatoencrypt32 = (datatoencrypt32 >> 16) + (datatoencrypt32 << 16);	
	write_32b(datatoencrypt32, encrypteddata);

	return;
}


/*
 * For Subaru, use SID 0x10 to start a diagnostic session to access programming commands.
 * Assumes everything is ok (conn state, etc)
 * @return 0 if successful
 */
int sub_sid10_diagsession() {
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq

	int errval;
	nisreq.data=txdata;	//super very essential !

	txdata[0]=0x10;  //SID 0x10 startCommunications command
	txdata[1]=0x85;  //programming session
	txdata[2]=0x02;
	nisreq.len=3;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len != 2) || (rxmsg->data[0] != 0x50)) {
		printf("got bad SID 0x10 startCommunications response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}

	diag_freemsg(rxmsg);
	return 0;
}


/*
 * For Subaru, use SID 0x34 to request download of data (ie: the kernel)
 * Assumes everything is ok (conn state, etc)
 * arguments are address for data to be downloaded to, and length of data
 * @return 0 if successful
 */
int sub_sid34_reqdownload(uint32_t dataaddr, uint32_t datalen) {
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq

	int errval;
	nisreq.data=txdata;	//super very essential !

	txdata[0]=0x34;  //SID 0x34 startCommunications command
	txdata[1]=(uint8_t) (dataaddr >> 16) & 0xFF; //download target at 0xFF [address] (first 0xFF is assumed)
	txdata[2]=(uint8_t) (dataaddr >> 8) & 0xFF;  
	txdata[3]=(uint8_t) dataaddr & 0xFF;  
	txdata[4]=0x04;  		 //uncompressed and encrypted
	txdata[5]=(uint8_t) (datalen >> 16) & 0xFF;  //Memory size is next three bytes
	txdata[6]=(uint8_t) (datalen >> 8) & 0xFF;
	txdata[7]=(uint8_t) datalen & 0xFF;  
	
	nisreq.len=8;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len != 2) || (rxmsg->data[0] != 0x74)) {
		printf("got bad SID 0x34 requestDownload response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}

	diag_freemsg(rxmsg);
	return 0;
}


/* For Subaru, transfer payload from *buf
 * len must be multiple of 4
 * Caller must have encrypted the payload
 * ret 0 if ok
 */
int sub_sid36_transferdata(uint32_t dataaddr, uint8_t *buf, uint32_t len) {
	uint8_t txdata[132];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;
	uint16_t blockno;
	uint16_t maxblocks;
	uint32_t blockaddr;

	len &= ~0x03;
	if (!buf || !len) return -1;

	maxblocks = (len - 1) >> 7;  // number of 128 byte blocks - 1

	txdata[0]=0x36;

	nisreq.data=txdata;

	for (blockno = 0; blockno <= maxblocks; blockno++) {

		blockaddr = dataaddr + blockno * 128;
		txdata[1] = (uint8_t) (blockaddr >> 16) & 0xFF;
		txdata[2] = (uint8_t) (blockaddr >> 8) & 0xFF;
		txdata[3] = (uint8_t) blockaddr & 0xFF;

		if (blockno == maxblocks) {
			memcpy(&txdata[4], buf, len);
			nisreq.len = 4 + len;
		}
		else {
			memcpy(&txdata[4], buf, 128);
			nisreq.len= 4 + 128;
			buf += 128;
			len -= 128;
		}

		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		
		if ((rxmsg->len != 1) || rxmsg->data[0] != 0x76) {
			printf("got bad SID 0x36 dataTransfer response : ");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
			return -1;
		}

		printf("\rSID36 block 0x%04X/0x%04X done",
				(unsigned) blockno, (unsigned) maxblocks);

	}

	printf("\n");
	return 0;
}


/* For Subaru, execute RAM Jump to address set in ROM (0xFFFF3004)
 * 
 * 
 * ret 0 if ok
 */
int sub_sid31_startRoutine() {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x31;
	txdata[1]=0x01;
	txdata[2]=0x01; // this should be 0x01 - intentionally wrong so ECU doesn't do jump
	nisreq.len=3;
	nisreq.data=txdata;

	/* RAMjump */
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0x71) {
		printf("got bad sid 31 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);

	return 0;
}


#define NRC_STRLEN 80	//too small and we get an assert() failure in smartcat() !!!
/** Return string for neg response code
 *
 * rxdata must point to the data frame (no headers), i.e. 0x7F <SID> <NRC>
 */
static const char *decode_nrc(uint8_t *rxdata) {
	struct diag_msg tmsg;
	static char descr[NRC_STRLEN]="";

	// Try Standard ISO14230 NRC
	tmsg.data = rxdata;
	tmsg.len = 3;	//assume rxdata contains a "7F <SID> <NRC>" message
	(void) diag_l3_iso14230_decode_response(&tmsg, descr, NRC_STRLEN);

	return descr;
}

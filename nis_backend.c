/*
 *	nisprog - Nissan ECU communications utility
 *
 * Copyright (c) 2014-2016 fenugrec
 *
 * Licensed under GPLv3
 *
 * back-end functions used by CLI commands, for Nissan ECUs
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stypes.h"

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_iso14230.h"	//for NRC decoding

#include "nisprog.h"
#include "nis_backend.h"
#include "nissutils/cli_utils/nislib.h"

#define CURFILE "nis_backend.c"	//HAAAX


/** Decode negative response code into a short error string.
 *
 * rxdata[] must contain at least 3 bytes, "7F <SID> <NRC>"
 * returns a static char * that must not be free'd !
 */
static const char *decode_nrc(uint8_t *rxdata);



/* algo1.
 * ( NPT_DDL2 algo (with key-in-ROM) ... niskey1.c )
 */
void genkey1(const uint8_t *seed8, uint32_t m, uint8_t *key) {
	uint32_t seed = reconst_32(seed8);
	write_32b(enc1(seed, m), key);	//write key in buffer.
	return;
}



/** Encrypt with the kline_at algo... niskey2.c
 * writes 4 bytes in buffer *key
 */
static void genkey2(const uint8_t *seed8, uint8_t *key) {
	uint32_t seed, ecx, xorloops;
	int ki;

	const uint32_t keytable[]={0x14FA3579, 0x27CD3964, 0x1777FE32, 0x9931AF12,
		0x75DB3A49, 0x19294CAA, 0x0FF18CD76, 0x788236D,
		0x5A6F7CBB, 0x7A992254, 0x0ADFD5414, 0x343CFBCB,
		0x0C2F51639, 0x6A6D5813, 0x3729FF68, 0x22A2C751};

	seed = reconst_32(seed8);

	ecx = (seed & 1)<<6 | (seed>>9 & 1)<<4 | (seed>>1 & 1)<<3;
	ecx |= (seed>>11 & 1)<<2 | (seed>>2 & 1)<<1 | (seed>>5 & 1);
	ecx += 0x1F;

	if (ecx <= 0) {
		printf("problem !!\n");
		return;
	}

	ki = (seed & 1)<<3 | (seed>>1 & 1)<<2 | (seed>>2 & 1)<<1 | (seed>>9 & 1);

	//printf("starting xorloop with ecx=0x%0X, ki=0x%0X\n", ecx, ki);

	for (xorloops=0; xorloops < ecx; xorloops++) {
		if (seed & 0x80000000) {
			seed += seed;
			seed ^= keytable[ki];
		} else {
			seed += seed;
		}
	}
	//here, the generated key is in "seed".

	write_32b(seed, key);

	return;
}


int get_ecuid(u8 *dest) {
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	uint8_t txdata[64];	//data for nisreq

	int errval;

	if (npstate == NP_DISC) {
		printf("Not connected to ECU\nTry \"nc\" first\n");
		return -1;
	}

	nisreq.data=txdata;	//super very essential !

	//request ECUID
	txdata[0]=0x1A;
	txdata[1]=0x81;
	nisreq.len=2;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if ((rxmsg->len < 7) || (rxmsg->data[0] != 0x5A)) {
		printf("got bad 1A response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	memcpy(dest, rxmsg->data + 2, 5);	//skip 0x5A 0x31
	dest[5]=0;	//null-terminate

	diag_freemsg(rxmsg);
	return 0;
}


/*
 * keyalg = 1 for "algo 1" (NPT_DDL2) + scode: widespread)
 * keyalg = 2 for alternate (genkey2, KLINE_AT)
 *
 * @return 0 if successful
 */
int sid27_unlock(int keyalg, uint32_t scode) {
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
	if ((rxmsg->len < 6) || (rxmsg->data[0] != 0x67)) {
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

	txdata[0]=0x27;
	txdata[1]=0x02;	//SendKey
	switch (keyalg) {
	case 1:
		genkey1(&rxmsg->data[2], scode, &txdata[2]);	//write key to txdata buffer
		//printf("; using NPT_DDL algo (scode=0x%08X), ", scode);
		break;
	case 2:
	default:
		genkey2(&rxmsg->data[2], &txdata[2]);	//write key to txdata buffer
		//printf("; using KLINE_AT algo, ");
		break;
	}
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
	printf("SID27:SUXXESS !!\n");

	diag_freemsg(rxmsg);
	return 0;
}




/* ret 0 if ok
 *
 * Assumes everything is ok (conn state, etc)
 */
int sid3480(void) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x34;
	txdata[1]=0x80;
	nisreq.len=2;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0x74) {
		printf("got bad 34 80 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);
	return 0;
}

/* transfer payload from *buf
 * len must be multiple of 32
 * Caller must have encrypted the payload
 * ret 0 if ok
 */
int sid36(uint8_t *buf, uint32_t len) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	int errval;
	uint16_t blockno;
	uint16_t maxblocks;

	len &= ~0x1F;
	if (!buf || !len) return -1;

	blockno = 0;
	maxblocks = (len / 32) - 1;

	txdata[0]=0x36;
	//txdata[1] and [2] is the 16bit block #
	txdata[3] = 0x20;		//block length; ignored by ECU
	nisreq.data=txdata;
	nisreq.len= 4 + 32;

	for (; len > 0; len -= 32, blockno += 1) {
		uint8_t rxbuf[10];	//can't remember what the actual sid 36 response looks like

		txdata[1] = blockno >> 8;
		txdata[2] = blockno & 0xFF;

		memcpy(&txdata[4], buf, 32);
		buf += 32;

		//rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("l2_send error!\n");
			return -1;
		}

		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, rxbuf, 3, 50);
		if (errval < 3) {
			printf("no response @ blockno %X\n", (unsigned) blockno);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}

		if (rxbuf[0] & 0x80) {
			printf("Problem: ECU responding with long headers ?\n");
			return -1;
		}
		if (rxbuf[1] != 0x76) {
			printf("got bad 36 response : ");
			if (rxbuf[1] == 0x7F) {
				printf("%s\n", decode_nrc(&rxbuf[1]));
			} else {
				diag_data_dump(stdout, rxbuf, errval);
				printf("\n");
			}
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}
		printf("\rSID36 block 0x%04X/0x%04X done",
				(unsigned) blockno, (unsigned) maxblocks);
		fflush(stdout);
	}
	printf("\n");
	fflush(stdout);
	return 0;
}

//send SID 37 transferexit request, ret 0 if ok
int sid37(uint16_t cks) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0x37;
	txdata[1]=cks >> 8;
	txdata[2]=cks & 0xFF;
	nisreq.len=3;
	nisreq.data=txdata;

	printf("sid37: sending ");
	diag_data_dump(stdout, txdata, 3);
	printf("\n");

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0x77) {
		printf("got bad 37 response : ");
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

/* RAMjump, takes care of SIDs BF 00 + BF 01
 * ret 0 if ok
 */
int sidBF(void) {
	uint8_t txdata[64];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	struct diag_msg *rxmsg=NULL;	//pointer to the reply
	int errval;

	txdata[0]=0xBF;
	txdata[1]=0;
	nisreq.len=2;
	nisreq.data=txdata;

	/* BF 00 : RAMjumpCheck */
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0xFF) {
		printf("got bad BF 00 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);

	/* BF 01 : RAMjumpCheck */
	txdata[1] = 1;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;

	if (rxmsg->data[0] != 0xFF) {
		printf("got bad BF 01 response : ");
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

	u8 nrc = rxdata[2];

	//XXX TODO : move this to nislib common defs someday
	//XXX TODO : determine which of these is SID dependant
#define C2_NRC_BAD_SID36_SEQ 0x90
#define C2_NRC_BAD_SID37_CKS 0x91
#define C2_NRC_LOWSYSV 0x92
#define C2_NRC_ADCG 0x93 //SID 27, "Additional charge condition" = wtf
#define C2_NRC_ENGRUN 0x94 //SID 27, fENGRUN - engine running
#define C2_NRC_LOADSW 0x95 //SID 27, electrical loads present

	// 1) try mfg-specific NRCs

	switch (nrc) {
	case C2_NRC_BAD_SID36_SEQ:
		return "Bad SID 36 block sequence / length";
		break;
	case C2_NRC_BAD_SID37_CKS:
		return "Bad SID 36/37 payload checksum";
		break;
	case C2_NRC_LOWSYSV:
		return "Low battery voltage";
		break;
	case C2_NRC_ADCG:
		return "Additional charge condition (?)";
		break;
	case C2_NRC_ENGRUN:
		return "Engine running";
		break;
	case C2_NRC_LOADSW:
		return "Electrical loads active";
		break;
	default:
		// 2) Try Standard ISO14230 NRC
		tmsg.data = rxdata;
		tmsg.len = 3;	//assume rxdata contains a "7F <SID> <NRC>" message
		(void) diag_l3_iso14230_decode_response(&tmsg, descr, NRC_STRLEN);
		return descr;
		break;
	}
	return descr;
}

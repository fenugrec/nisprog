/*
 *	nisprog - Nissan ECU communications utility
 *
 * Copyright (c) 2014-2016 fenugrec
 *
 * Licensed under GPLv3
 *
 * back-end functions used by CLI commands
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stypes.h"

#include "diag.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_os.h"
#include "diag_tty.h"	//for setspeed
#include "diag_l2_iso14230.h" 	//needed to force header type (nisprog)

#include "nisprog.h"
#include "np_backend.h"
#include "nissutils/cli_utils/nislib.h"


/* flash block definitions */
const struct flashblock fblocks_7058[] = {
	{0x00000000,	0x00001000},
	{0x00001000,	0x00001000},
	{0x00002000,	0x00001000},
	{0x00003000,	0x00001000},
	{0x00004000,	0x00001000},
	{0x00005000,	0x00001000},
	{0x00006000,	0x00001000},
	{0x00007000,	0x00001000},
	{0x00008000,	0x00018000},
	{0x00020000,	0x00020000},
	{0x00040000,	0x00020000},
	{0x00060000,	0x00020000},
	{0x00080000,	0x00020000},
	{0x000A0000,	0x00020000},
	{0x000C0000,	0x00020000},
	{0x000E0000,	0x00020000},
};

const struct flashblock fblocks_7055[] = {
	{0x00000000,	0x00001000},
	{0x00001000,	0x00001000},
	{0x00002000,	0x00001000},
	{0x00003000,	0x00001000},
	{0x00004000,	0x00001000},
	{0x00005000,	0x00001000},
	{0x00006000,	0x00001000},
	{0x00007000,	0x00001000},
	{0x00008000,	0x00008000},
	{0x00010000,	0x00010000},
	{0x00020000,	0x00010000},
	{0x00030000,	0x00010000},
	{0x00040000,	0x00010000},
	{0x00050000,	0x00010000},
	{0x00060000,	0x00010000},
	{0x00070000,	0x00010000},
};


const struct flashdev_t flashdevices[] = {
	{ "7055", 512 * 1024, 16, fblocks_7055 },
	{ "7058", 1024 * 1024, 16, fblocks_7058 },
	{ NULL, 0, 0, NULL },
};



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
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	printf("Trying SID 27, got seed: ");
	diag_data_dump(stdout, &rxmsg->data[2], 4);

	txdata[0]=0x27;
	txdata[1]=0x02;	//SendKey
	switch (keyalg) {
	case 1:
		genkey1(&rxmsg->data[2], scode, &txdata[2]);	//write key to txdata buffer
		printf("; using NPT_DDL algo (scode=0x%0X), ", scode);
		break;
	case 2:
	default:
		genkey2(&rxmsg->data[2], &txdata[2]);	//write key to txdata buffer
		printf("; using KLINE_AT algo, ");
		break;
	}
	diag_freemsg(rxmsg);

	printf("to send key ");
	diag_data_dump(stdout, &txdata[2], 4);
	printf("\n");

	nisreq.len=6; //27 02 K K K K
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL)
		return -1;
	if (rxmsg->data[0] != 0x67) {
		printf("got bad 27 02 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	printf("SUXXESS !!\n");

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

		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, 3, 50);
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
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}
		printf("\rSID36 block 0x%04X/0x%04X done",
				(unsigned) blockno, (unsigned) maxblocks);
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
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
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






/*** CRC16 implementation adapted from Lammert Bies
 * https://www.lammertbies.nl/comm/info/crc-calculation.html
 *
 *
 */
#define NPK_CRC16	0xBAAD	//koopman, 2048bits (256B)
static bool crc_tab16_init = 0;
static u16 crc_tab16[256];

static void init_crc16_tab( void ) {
	u32 i, j;
	u16 crc, c;

	for (i=0; i<256; i++) {
		crc = 0;
		c   = (u16) i;

		for (j=0; j<8; j++) {
			if ( (crc ^ c) & 0x0001 ) crc = ( crc >> 1 ) ^ NPK_CRC16;
			else                      crc =   crc >> 1;
			c = c >> 1;
		}
		crc_tab16[i] = crc;
	}

	crc_tab16_init = 1;

}  /* init_crc16_tab */


static u16 crc16(const u8 *data, u32 siz) {
	u16 crc;

	if ( ! crc_tab16_init ) init_crc16_tab();

	crc = 0;

	while (siz > 0) {
		u16 tmp;
		u8 nextval;

		nextval = *data++;
		tmp =  crc       ^ nextval;
		crc = (crc >> 8) ^ crc_tab16[ tmp & 0xff ];
		siz -= 1;
	}
	return crc;
}

/** compare CRC of source data at *src to ROM
 * the area starting at src[0] is compared to the area of ROM
 * starting at <start>, for a total of <len> bytes (rounded up)
 *
 * Caller must have validated parameters
 *
 * @param modified: result of crc check is written to that variable
 * @return 0 if comparison completed correctly
 */
#define ROMCRC_NUMCHUNKS 4
#define ROMCRC_CHUNKSIZE 256
#define ROMCRC_ITERSIZE (ROMCRC_NUMCHUNKS * ROMCRC_CHUNKSIZE)
#define ROMCRC_LENMASK ((ROMCRC_NUMCHUNKS * ROMCRC_CHUNKSIZE) - 1)	//should look like 0x3FF
static int check_romcrc(const uint8_t *src, uint32_t start, uint32_t len, bool *modified) {
	uint8_t txdata[6];	//data for nisreq
	struct diag_msg nisreq={0};	//request to send
	uint8_t rxbuf[10];
	int errval;
	uint16_t chunko;
	//uint16_t maxchunks;

	len = (len + ROMCRC_LENMASK) & ~ROMCRC_LENMASK;

	chunko = start / ROMCRC_CHUNKSIZE;

		//request format : <SID_CONF> <SID_CONF_CKS1> <CNH> <CNL> <CRC0H> <CRC0L> ...<CRC3H> <CRC3L>
		//verify if <CRCH:CRCL> hash is valid for n*256B chunk of the ROM (starting at <CNH:CNL> * 256)
#define SID_CONF 0xBE
	unsigned txi;
	txdata[0]=SID_CONF;
	txdata[1]=0x03;

	nisreq.data=txdata;


	for (; len > 0; len -= ROMCRC_ITERSIZE, chunko += ROMCRC_NUMCHUNKS) {
		txi = 2;
		txdata[txi++] = chunko >> 8;
		txdata[txi++] = chunko & 0xFF;

		//fill the request with n*CRCs
		unsigned chunk_cnt;
		for (chunk_cnt = 0; chunk_cnt < ROMCRC_NUMCHUNKS; chunk_cnt++) {
			u16 chunk_crc = crc16(src, ROMCRC_CHUNKSIZE);
			src += ROMCRC_CHUNKSIZE;
			txdata[txi++] = chunk_crc >> 8;
			txdata[txi++] = chunk_crc & 0xFF;
		}
		nisreq.len = txi;


		//rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		errval = diag_l2_send(global_l2_conn, &nisreq);
		if (errval) {
			printf("\nl2_send error!\n");
			return -1;
		}

		//responses :	01 FC FD for good CRC
		//				03 7F FC 77 <cks> for bad CRC
		// anything else is an error that causes abort
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, 3, 50);
		if (errval != 3) {
			printf("\nno response @ chunk %X\n", (unsigned) chunko);
			goto badexit;
		}

		if (rxbuf[1] == SID_CONF + 0x40) {
			continue;
		}
		//so, it's a 03 7F FC xx <cks> response. Get remainder of packet
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf+3, 2, 50);
		if (errval != 2) {
			printf("\nweirdness @ chunk %X\n", (unsigned) chunko);
			goto badexit;
		}

		if ((rxbuf[2] != SID_CONF) || (rxbuf[3] != 0x77)) {
			printf("\ngot bad SID_FLASH_CKS1 response : ");
			goto badexit;
		}
		//confirmed bad CRC, we can exit
		*modified = 1;
		return 0;
	}	//for

	*modified = 0;
	return 0;

badexit:
	diag_data_dump(stdout, rxbuf, sizeof(rxbuf));
	printf("\n");
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	return -1;
}


int get_changed_blocks(const uint8_t *src, const uint8_t *orig_data, const struct flashdev_t *fdt, bool *modified) {

	unsigned blockno;

	printf("\n");
	for (blockno = 0; blockno < (fdt->numblocks); blockno++) {
		u32 bs, blen;
		bs = fdt->fblocks[blockno].start;
		blen = fdt->fblocks[blockno].len;

		printf("\rchecking block %02u/%02u (%06lX-%06lX)...",
				blockno, fdt->numblocks -1, (unsigned long) bs, (unsigned long) bs + blen -1);
		/* compare with caller's buffer if provided: */
		if (orig_data) {
			if (memcmp(&src[bs], &orig_data[bs], blen) == 0) {
				modified[blockno] = 0;
			} else {
				modified[blockno] = 1;
			}
		} else {
			/* otherwise do CRC comparison with ECU */
			if (check_romcrc(&src[bs], bs, blen, &modified[blockno])) {
				return -1;
			}
		}
	}
	printf(" done.\n");
	return 0;
}

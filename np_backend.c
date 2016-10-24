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


/** do SID 34 80 transaction, ret 0 if ok
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

		/* this will always time out since the response is probably always 5 bytes */
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d, NULL, rxbuf, sizeof(rxbuf), 25);
		if (errval <= 3) {
			printf("no response @ blockno %X\n", (unsigned) blockno);
			(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
			return -1;
		}

		if (rxbuf[0] & 0x80) {
			//with address : response looks like "<len | 0x80> <src> <dest> <resp>"
			rxbuf[0] = rxbuf[3];
		} else {
			//no address : "<len> <resp> <cks>"
			rxbuf[0] = rxbuf[1];
		}
		if (rxbuf[0] != 0x76) {
			printf("got bad 36 response : ");
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
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

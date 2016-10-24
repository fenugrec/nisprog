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

/** SID 1A GetECUID
 * writes 5 chars + 0x00 to *dest
 *
 * Ret 0 if ok
 */
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

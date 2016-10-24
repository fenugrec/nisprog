/* stuff copied from freediag scantool.c just to make this compile */


#include "diag.h"

#define RQST_HANDLE_WATCH	1	/* Watching, add timestamp */
#define RQST_HANDLE_DECODE	2	/* Just decode what arrived */
const int _RQST_HANDLE_WATCH = RQST_HANDLE_WATCH;  //Watching: add timestamp
const int _RQST_HANDLE_DECODE = RQST_HANDLE_DECODE; 	//Just decode what arrived

void
l2raw_data_rcv(UNUSED(void *handle), struct diag_msg *msg)
{
	/*
	 * Layer 2 call back, just print the data, this is used if we
	 * do a "read" and we haven't yet added a L3 protocol
	 */
	diag_printmsg(stderr, msg, 0);
	return;
}

void
j1979_data_rcv(void *handle, struct diag_msg *msg)
{
		(void) handle;
		(void) msg;
		return;
}
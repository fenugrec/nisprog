/* stuff copied from freediag scantool.c just to make this compile */


#include "diag.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "scantool_cli.h"

#define RQST_HANDLE_WATCH	1	/* Watching, add timestamp */
#define RQST_HANDLE_DECODE	2	/* Just decode what arrived */
const int _RQST_HANDLE_WATCH = RQST_HANDLE_WATCH;  //Watching: add timestamp
const int _RQST_HANDLE_DECODE = RQST_HANDLE_DECODE; 	//Just decode what arrived

/* garbage to make freediag cli compile */
const struct cmd_tbl_entry test_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};
const struct cmd_tbl_entry vag_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};
const struct cmd_tbl_entry dyno_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};
const struct cmd_tbl_entry v850_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};

void l2raw_data_rcv(UNUSED(void *handle), struct diag_msg *msg) {
	/*
	 * Layer 2 call back, just print the data, this is used if we
	 * do a "read" and we haven't yet added a L3 protocol
	 */
	diag_printmsg(stderr, msg, 0);
	return;
}

/* dummy func, we're not using J1979 */
void j1979_data_rcv(void *handle, struct diag_msg *msg) {
		(void) handle;
		(void) msg;
		return;
}

/* dummy func, we're not using J1979 */
struct diag_l3_conn;
int l3_do_send(struct diag_l3_conn *d_conn, void *data, size_t len, void *handle) {
		(void) d_conn;
		(void) data;
		(void) len;
		(void) handle;
		fprintf(stderr, "Error : L3 send code neutralized\n");
		return 0;
}


/* identicopy of scantool.c */
int
l2_do_send(struct diag_l2_conn *d_conn, void *data, size_t len, void *handle)
{
	struct diag_msg msg={0};
	int rv;
	if (len > 255)
		return DIAG_ERR_GENERAL;

	/* Put in src/dest etc, L2 may override/ignore them */
	msg.src = global_cfg.src;
	msg.dest = global_cfg.tgt;

	msg.len = len;
	msg.data = (uint8_t *)data;
	diag_l2_send(d_conn, &msg);

	/* And get response(s) */
	rv = diag_l2_recv(d_conn, 300, l2raw_data_rcv, handle);

	return rv;
}

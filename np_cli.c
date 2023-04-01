/*
 *	nisprog - Nissan ECU communications utility
 *
 * (c) 2014-2016 fenugrec
 * Licensed under GPLv3
 *
 * the CLI commands are implemented in here.
 */


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stypes.h"

#include "diag.h"
#include "diag_l0.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_os.h"
#include "diag_tty.h"   //for setspeed
#include "diag_l2_iso14230.h"   //needed to force header type (nisprog)

#include "scantool_cli.h"

#include "nisprog.h"
#include "nis_backend.h"
#include "npk_backend.h"
#include "ssm_backend.h"
#include "nissutils/cli_utils/nislib.h"
#include "nissutils/cli_utils/ecuid_list.h"
#include "npkern/iso_cmds.h"

#define CURFILE "np_cli.c"  //XXXXX TODO: fix VS automagic macro setting

#define NPK_SPEED 62500 //bps default speed for npkern kernel


typedef long nparam_val;    //type of .val member

/** simpler parameter unit than diag_cfgi */
struct nparam_t {
	long val;
	const char *shortname;
	const char *descr;
	long min;
	long max;   //validation : (val >= min) && (val <= max)
};


static struct nparam_t nparam_p3 = {.val = 5, .shortname = "p3", .descr = "P3 time before new request (ms)",
	                                .min = 0, .max = 500};
static struct nparam_t nparam_rxe = {.val = 20, .shortname = "rxe", .descr = "Read timeout offset. Adjust to eliminate timeout errors",
	                                 .min = -20, .max = 500};
static struct nparam_t nparam_eepr = {.val = 0, .shortname = "eepr", .descr = "eeprom_read() function address",
	                                  .min = 0, .max = 2048L * 1024};
static struct nparam_t nparam_kspeed = {.val = NPK_SPEED, .shortname = "kspeed", .descr = "kernel comms speed used by \"initk\" command",
	                                    .min = 100, .max = 65000};
static struct nparam_t *nparams[] = {
	&nparam_p3,
	&nparam_rxe,
	&nparam_eepr,
	&nparam_kspeed,
	NULL
};

struct nisecu_t nisecu;

/** some static data for in here only */
static struct keyset_t customkey;

/** fwd decls **/
static int npkern_init(void);
static int npk_dump(FILE *fpl, uint32_t start, uint32_t len, bool eep);
static int dump_fast(FILE *outf, const uint32_t start, uint32_t len);
static uint32_t read_ac(uint8_t *dest, uint32_t addr, uint32_t len);
static int npk_RMBA(uint8_t *dest, uint32_t addr, uint32_t len);
static bool set_keyset(u32 s27k);



/* called every time a parameter is changed from the UI.
 * For some params, the value is only used in certain places,
 * and therefore doesn't need to be handled in here
 */
static void update_params(void) {
	if (global_l2_conn) {
		global_l2_conn->diag_l2_p3min = (u16) nparam_p3.val;
		global_l2_conn->diag_l2_p4min=0;
	}
	return;
}

/* npconf <paramname> <value>
 */
int cmd_npconf(int argc, char **argv) {
	struct nparam_t *npt;
	nparam_val tempval;
	bool found = 0;
	bool helping = 0;

	if ((argc <= 1) || (argc > 3)) {
		return CMD_USAGE;
	}

	if (argv[1][0] == '?') {
		helping = 1;
		printf("param\tvalue\tdescription\n");
	}

	// find param name in list
	unsigned i;
	for (i = 0; nparams[i]; i++) {
		npt = nparams[i];
		if (helping) {
			printf("%s\t%ld\t%s\n", npt->shortname, npt->val, npt->descr);
			continue;
		}
		if (strcmp(npt->shortname, argv[1]) == 0) {
			found = 1;
			break;
		}
	}

	if (helping) {
		return CMD_USAGE;
	}

	if (!found) {
		printf("Unknown param \"%s\"\n", argv[1]);
		return CMD_FAILED;
	}

	if (argc == 2) {
		//no new value : just print current setting
		printf("%s is currently %ld (0x%lX)\n", npt->shortname,
		       npt->val, npt->val);
		return CMD_OK;
	}

	tempval = htoi(argv[2]);

	if ((tempval < npt->min) || (tempval > npt->max)) {
		printf("Error, requested value (%ld / 0x%lX) out of bounds !\n",
		       (long) tempval, (long) tempval);
		return CMD_FAILED;
	}
	npt->val = tempval;
	update_params();
	printf("\t%s set to %ld (0x%lX).\n", argv[1], npt->val, npt->val);
	return CMD_OK;
}


/* "dumpmem <file> <start> <len> [eep]" */
int cmd_dumpmem(int argc, char **argv) {
	u32 start, len;
	FILE *fpl;
	bool eep = 0;

	if (npstate == NP_DISC) {
		printf("Not connected !\n");
		return CMD_FAILED;
	}

	if ((argc < 4) || (argc > 5)) {
		return CMD_USAGE;
	}

	if (argc == 5) {
		if (strcmp("eep", argv[4]) == 0) {
			eep = 1;
			if (npstate != NP_NPKCONN) {
				printf("Kernel must be running for reading EEPROM. Try \"runkernel\" or \"initk\"\n");
				return CMD_FAILED;
			}
			if (!nparam_eepr.val) {
				printf("Must set eeprom read function address first ! See \"npconf ?\"\n");
				return CMD_FAILED;
			}
			if (set_eepr_addr((u32) nparam_eepr.val)) {
				printf("could not set eep_read() address!\n");
				return CMD_FAILED;
			}
		} else {
			printf("did not recognize \"%s\"\n", argv[4]);
			return CMD_FAILED;
		}
	}

	//TODO : check for overwrite / append ?

	if ((fpl = fopen(argv[1], "wb"))==NULL) {
		printf("Cannot open %s !\n", argv[1]);
		return CMD_FAILED;
	}

	start = (uint32_t) htoi(argv[2]);
	len = (uint32_t) htoi(argv[3]);

	if ((start == len) && (start == 0)) {
		//special mode : dump all ROM as specified by device type.
		const struct flashdev_t *fdt = nisecu.flashdev;
		if (!fdt) {
			printf("device type not set. Try setdev, or specify bounds manually.\n");
			fclose(fpl);
			return CMD_FAILED;
		}
		len = fdt->romsize;
	}

	/* Dispatch according to current state */

	if (npstate == NP_NPKCONN) {
		if (npk_dump(fpl, start, len, eep)) {
			fclose(fpl);
			return CMD_FAILED;
		}
		fclose(fpl);
		return CMD_OK;
	}
	// npstate == NP_NORMALCONN:
	if (dump_fast(fpl, start, len)) {
		fclose(fpl);
		return CMD_FAILED;
	}
	fclose(fpl);
	return CMD_OK;
}

#define KEY_CANDIDATES 3
#define KEY_MAXDIST 10  //do not use keys that are way off
void autoselect_keyset(void) {
	unsigned i;
	u32 s27k;
	struct ecuid_keymatch_t kcs[KEY_CANDIDATES];

	ecuid_getkeys((const char *) nisecu.ecuid, kcs, KEY_CANDIDATES);

	printf("Key candidate\tdist (smaller is better)\n");
	for (i = 0; i < KEY_CANDIDATES; i++) {
		printf("%u: 0x%08lX\t%d\n",i, (unsigned long) kcs[i].key, kcs[i].dist);
	}
	printf("\n");
	// TODO : allow user selection

	if (kcs[0].dist > KEY_MAXDIST) {
		printf("ECUID matching was not conclusive, keyset unknown. Try gk or setkeys\n");
		return;
	}

	s27k = kcs[0].key;

	(void) set_keyset(s27k);

	printf("Using best choice, SID27 key=%08lX. Use \"setkeys\" to change if required.\n",
	       (unsigned long) s27k);

	return;
}

/* setdev <device_#> */
int cmd_setdev(int argc, char **argv) {
	bool helping = 0;
	unsigned idx;
	const struct flashdev_t *fdt = nisecu.flashdev;

	if (argc != 2) {
		if (fdt) {
			printf("current setting : %s\n", fdt->name);
		}
		return CMD_USAGE;
	}

	if (argv[1][0] == '?') {
		helping = 1;
		printf("\tname\tROM size\n");
	}

	for (idx=0; flashdevices[idx].name; idx++) {
		if (helping) {
			printf("\t%s\t%uk\n", flashdevices[idx].name, (unsigned) flashdevices[idx].romsize / 1024);
			continue;
		}
		if (strcmp(flashdevices[idx].name, argv[1]) == 0) {
			nisecu.flashdev = &flashdevices[idx];
			printf("now using %s.\n", flashdevices[idx].name);
			return CMD_OK;
		}
	}
	if (helping) {
		return CMD_USAGE;
	}

	printf("Invalid device, see list with \"setdev ?\"\n");
	return CMD_FAILED;
}

/* setkeys <sid27key> [<sid36key1>] */
int cmd_setkeys(int argc, char **argv) {
	const struct keyset_t *pks;

	if (argc == 2) {
		if (argv[1][0] == '?') {
			return CMD_USAGE;
		}
	}
	if ((argc < 2) || (argc > 3)) {
		return CMD_USAGE;
	}

	u32 s27k = (u32) htoi(argv[1]);
	customkey.s27k = s27k;
	if (argc == 3) {
		// use specified keys : don't lookup in known_keys[]
		customkey.s36k1 = (u32) htoi(argv[2]);
		nisecu.keyset = &customkey;
		goto goodexit;
	}

	if (set_keyset(s27k)) {
		goto goodexit;
	}

	printf("Does not match a known keyset; you will need to provide both s27 and s36 keys\n");
	return CMD_FAILED;

goodexit:
	pks = nisecu.keyset;
	printf("Now using SID27 key=%08lX, SID36 key1=%08lX\n",
	       (unsigned long) pks->s27k, (unsigned long) pks->s36k1);
	return CMD_OK;
}


int cmd_initk(int argc, char **argv) {
	const char *npk_id;

	(void) argv;
	if (argc > 1) {
		return CMD_USAGE;
	}

	if ((npstate == NP_DISC) ||
	    (global_state == STATE_IDLE)) {
		printf("Error : not connected\n");
		return CMD_FAILED;
	}

	if (npkern_init()) {
		printf("npkern_init() error\n");
		return CMD_FAILED;
	}

	npstate = NP_NPKCONN;

	npk_id = get_npk_id();
	if (npk_id) {
		printf("Connected to kernel: %s\n", npk_id);
	}

	return CMD_OK;
}



int cmd_npconn(int argc, char **argv) {
	(void) argv;
	if (argc > 1) {
		return CMD_USAGE;
	}

	if ((npstate != NP_DISC) ||
	    (global_state != STATE_IDLE)) {
		printf("Error : already connected\n");
		return CMD_FAILED;
	}

	nisecu_cleardata(&nisecu);


	struct diag_l2_conn *d_conn;
	struct diag_l0_device *dl0d = global_dl0d;
	int rv;
	flag_type flags = 0;

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return CMD_FAILED;
	}

	if (global_cfg.L2proto != DIAG_L2_PROT_ISO14230) {
		printf("L2 protocol must be iso14230 to use Nissan commands ! try \"set l2protocol\"\n");
		return CMD_FAILED;
	}

	/* Open interface using current L1 proto and hardware */
	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "Open failed for protocol %d on %s\n",
		        global_cfg.L1proto, dl0d->dl0->shortname);
		return CMD_FAILED;
	}

	if (global_cfg.addrtype) {
		flags = DIAG_L2_TYPE_FUNCADDR;
	} else {
		flags = 0;
	}

	flags |= (global_cfg.initmode & DIAG_L2_TYPE_INITMASK);

	d_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
	                                     flags, global_cfg.speed, global_cfg.tgt, global_cfg.src);

	if (d_conn == NULL) {
		(void) diag_geterr();
		diag_l2_close(dl0d);
		printf("L2 StartComms failed\n");
		return CMD_FAILED;
	}

	/* Connected ! */

	global_l2_conn = d_conn;
	global_state = STATE_CONNECTED;
	npstate = NP_NORMALCONN;

	update_params();

	printf("Connected to ECU !\n");

	struct diag_l2_14230 * dlproto; // for bypassing headers
	dlproto = (struct diag_l2_14230 *)global_l2_conn->diag_l2_proto_data;
	if (dlproto->modeflags & ISO14230_SHORTHDR) {
		dlproto->modeflags &= ~ISO14230_LONGHDR;    //deactivate long headers
	} else {
		printf("Short headers not supported by ECU ! Have you \"set addrtype phys\" ?"
		       "Some stuff will not work.");
	}

	if (get_ecuid(nisecu.ecuid)) {
		printf("Couldn't get ECUID ? Verify settings, connection mode etc.\n");
		return CMD_FAILED;
	}
	printf("ECUID: %s\n", (char *) nisecu.ecuid);
	autoselect_keyset();

	return CMD_OK;
}


int cmd_spconn(int argc, char **argv) {
	(void) argv;
	if (argc > 1) {
		return CMD_USAGE;
	}

	if ((npstate != NP_DISC) ||
	    (global_state != STATE_IDLE)) {
		printf("Error : already connected\n");
		return CMD_FAILED;
	}

	nisecu_cleardata(&nisecu);

	struct diag_l2_conn *d_conn;
	struct diag_l0_device *dl0d = global_dl0d;
	int i, rv;
	flag_type flags = 0;
	struct diag_l2_14230 *dp;
	const struct diag_l2_proto *dl2p;

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return CMD_FAILED;
	}

	if (global_cfg.L2proto != DIAG_L2_PROT_RAW) {
		printf("L2 protocol must start as RAW to communicate with Subaru ECU ! try \"set l2protocol\"\n");
		return CMD_FAILED;
	}

	/* Open interface using current L1 proto and hardware */
	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "Open failed for protocol %d on %s\n",
		        global_cfg.L1proto, dl0d->dl0->shortname);
		return CMD_FAILED;
	}

	if (global_cfg.addrtype) {
		flags = DIAG_L2_TYPE_FUNCADDR;
	} else {
		flags = 0;
	}

	flags |= (global_cfg.initmode & DIAG_L2_TYPE_INITMASK);

	d_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
	                                     flags, global_cfg.speed, global_cfg.tgt, global_cfg.src);

	if (d_conn == NULL) {
		(void) diag_geterr();
		diag_l2_close(dl0d);
		printf("L2 StartComms failed\n");
		return CMD_FAILED;
	}

	//At this point we have a valid RAW connection and need to update d_conn manually to change to ISO14230 with Subaru headers, checksum
	//The general initialisation would have set d_conn->diag_link, ->l2proto, ->diag_l2_type, ->diag_l2_srcaddr, ->diag_l2_destaddr,
	//->diag_l2_p1 2 2e 3 4 min max, ->tinterval, ->tlast, ->diag_l2_state
	//The RAW initialisation would have set diag_serial_settings and d_conn->diag_l2_destaddr and ->diag_l2_srcaddr

	printf("L2 RAW detected, changing to L2 ISO14230\n");

	global_cfg.L2proto = DIAG_L2_PROT_ISO14230;

	for (i=0; l2proto_list[i] ; i++) {
		dl2p = l2proto_list[i];
		if (dl2p->diag_l2_protocol == global_cfg.L2proto) {
			d_conn->l2proto = dl2p;
			break;
		}
	}

	rv = diag_calloc(&dp, 1);
	if (rv != 0) {
		return CMD_FAILED;
	}

	d_conn->diag_l2_proto_data = (void *)dp;

	dp->srcaddr = global_cfg.src;
	dp->dstaddr = global_cfg.tgt;
	dp->first_frame = 0;
	dp->monitor_mode = 0;
	d_conn->diag_l2_kb1 = 0x8F;
	d_conn->diag_l2_kb2 = 0xEA; //length byte required, address bytes required (ie) 4 byte headers
	d_conn->diag_l2_physaddr = global_cfg.src;
	dp->state = STATE_ESTABLISHED;
	dp->modeflags = 6; //length byte required, address bytes required (ie) 4 byte headers
	printf("Change to ISO14230 successful\n");

	global_l2_conn = d_conn;
	global_state = STATE_CONNECTED;
	npstate = NP_NORMALCONN;

	update_params();

	if(sub_sid81_startcomms()) {
		printf("SID 0x81 startCommunications failed. Verify settings, connection mode etc.\n");
		global_l2_conn = NULL;
		global_state = STATE_IDLE;
		npstate = NP_DISC;
		return CMD_FAILED;
	}

	/* Connected ! */

	printf("\nConnected to ECU and ready for SSM or SID Commands\n");

	if (sub_get_ecuid(nisecu.ecuid)) {
		printf("Couldn't get ECUID ? Verify settings, connection mode etc.\n");
		return CMD_FAILED;
	}
	printf("\nECUID: ");
	for (i=0; i < 5; i++) {
		printf("%02x ", nisecu.ecuid[i]);
	}
	printf("\n");

	return CMD_OK;
}


int cmd_npdisc(UNUSED(int argc), UNUSED(char **argv)) {
	if ((npstate == NP_DISC) ||
	    (global_state == STATE_IDLE)) {
		return CMD_OK;
	}

	if (npstate == NP_NPKCONN) {
		printf("\n****** Kernel still running on ECU. Do you want to stop it before disconnecting ?\n"
				"n : \t\t No, just disconnect and let kernel run (usually not what you want)\n"
				"any other key :\t Yes, stopkernel first (preferred)\n");

		char *inp = basic_get_input("> ", stdin);
		if (!inp) {
			//if user feeds an EOF, don't do anything
			return CMD_FAILED;
		}
		char answer=inp[0];
		free(inp);

		switch (answer) {
		case 'n':	//fallthrough
		case 'N':
			break;
		default:
			printf("\n\tStopping kernel and rebooting ECU. To avoid the previous prompt,\n"
			"\trun 'stopkernel' first.\n");
			cmd_stopkernel(1, NULL);
			break;
		}
	}

	diag_l2_StopCommunications(global_l2_conn);
	diag_l2_close(global_dl0d);

	global_l2_conn = NULL;
	global_state = STATE_IDLE;
	npstate = NP_DISC;

	return CMD_OK;
}

int cmd_stopkernel(int argc, UNUSED(char **argv)) {
	uint8_t txdata[1];
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;

	if (npstate != NP_NPKCONN) {
		return CMD_OK;
	}

	if (argc != 1) {
		return CMD_USAGE;
	}

	printf("Resetting ECU and closing connection.\n");

	txdata[0]=SID_RESET;
	nisreq.len=1;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return CMD_FAILED;
	}

	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	(void) cmd_npdisc(0, NULL);

	return CMD_OK;
}


//np 1: try start diagsession, Nissan Repro style +
// accesstimingparams (get limits + setvals)
static int np_1(UNUSED(int argc), UNUSED(char **argv)) {

	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;

	txdata[0]=0x10;
	txdata[1]=0x85;
	txdata[2]=0x14;
	nisreq.len=3;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return CMD_FAILED;
	}
	if (rxmsg->data[0] != 0x50) {
		printf("got bad response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("StartDiagsess: got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);

	//try accesstimingparam : read limits
	txdata[0]=0x83;
	txdata[1]=0x0;  //read limits
	nisreq.len=2;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return CMD_FAILED;
	}
	printf("\nAccesTiming : read limits got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);

	//try ATP : read settings
	txdata[0]=0x83;
	txdata[1]=0x02;
	nisreq.len=2;
	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return CMD_FAILED;
	}
	printf("\nAccesTiming : read settings got ");
	diag_data_dump(stdout, rxmsg->data, rxmsg->len);
	diag_freemsg(rxmsg);
	printf("\n");
	return CMD_OK;
}
//np 2 :
static int np_2(int argc, char **argv) {
	//np 2 <addr> : read 1 byte @ addr, with SID A4
	// TX {07 A4 <A0> <A1> <A2> <A3> 04 01 cks}, 9 bytes on bus
	// RX {06 E4 <A0> <A1> <A2> <A3> <BB> cks}, 8 bytes
	// total traffic : 17 bytes for 1 rx'd byte - very slow
	//printf("Attempting to read 1 byte @ 000000:\n");
	uint8_t txdata[64]; //data for nisreq
	uint32_t addr;
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;

	if (argc != 3) {
		printf("usage: npt 2 <addr>: read 1 byte @ <addr>\n");
		return CMD_USAGE;
	}
	if (sscanf(argv[2], "%x", &addr) != 1) {
		printf("Did not understand %s\n", argv[2]);
		return CMD_USAGE;
	}
	txdata[0]=0xA4;
	txdata[4]= (uint8_t) (addr & 0xFF);
	txdata[3]= (uint8_t) (addr >> 8) & 0xFF;
	txdata[2]= (uint8_t) (addr >> 16) & 0xFF;
	txdata[1]= (uint8_t) (addr >> 24) & 0xFF;
	txdata[5]=0x04; //TXM
	txdata[6]=0x01; //NumResps
	nisreq.len=7;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		return CMD_FAILED;
	}
	if ((rxmsg->data[0] != 0xE4) || (rxmsg->len != 6)) {
		printf("got bad A4 response : ");
		diag_data_dump(stdout, rxmsg->data, rxmsg->len);
		printf("\n");
		diag_freemsg(rxmsg);
		return CMD_FAILED;
	}
	printf("Got: 0x%02X\n", rxmsg->data[5]);
	diag_freemsg(rxmsg);
	return CMD_OK;
}


/** np 5 : fast dump <len> bytes @<start> to already-opened <outf>;
 * uses fast read technique (receive from L1 direct)
 *
 * return CMD_* , caller must close outf
 */
static int dump_fast(FILE *outf, const uint32_t start, uint32_t len) {
	//SID AC + 21 technique.
	// AC 81 {83 GGGG} {83 GGGG} ... to load addresses, (5*n + 4) bytes on bus
	// RX: {EC 81}, 4 bytes
	// TX: {21 81 04 01} to dump data (6 bytes)
	// RX: {61 81 <n*data>} (4 + n) bytes.
	// Total traffic : (6*n + 18) bytes on bus for <n> bytes RX'd
	struct diag_msg nisreq={0}; //request to send
	uint8_t txdata[64]; //data for nisreq
	int errval;
	int retryscore=100; //successes increase this up to 100; failures decrease it.
	uint8_t hackbuf[70];
	int extra;  //extra bytes to purge
	uint32_t addr, nextaddr, maxaddr;
	unsigned long total_chron;

	nextaddr = start;
	maxaddr = start + len - 1;

	if (!outf) {
		return CMD_FAILED;
	}

	nisreq.data=txdata;
	total_chron = diag_os_getms();
	while (retryscore >0) {

		unsigned int linecur=0; //count from 0 to 11 (12 addresses per request)

		int txi;    //index into txbuf for constructing request


		printf("Starting dump from 0x%08X to 0x%08X.\n", nextaddr, maxaddr);

		txdata[0]=0xAC;
		txdata[1]=0x81;
		nisreq.len = 2; //AC 81 : 2 bytes so far
		txi=2;
		linecur = 0;

		unsigned long t0, chrono;
		unsigned chron_cnt = 0; //how many bytes between refreshes
		t0 = diag_os_getms();

		for (addr=nextaddr; addr <= maxaddr; addr++) {
			txdata[txi++]= 0x83;        //field type
			txdata[txi++]= (uint8_t) (addr >> 24) & 0xFF;
			txdata[txi++]= (uint8_t) (addr >> 16) & 0xFF;
			txdata[txi++]= (uint8_t) (addr >> 8) & 0xFF;
			txdata[txi++]= (uint8_t) (addr & 0xFF);
			nisreq.len += 5;
			linecur += 1;

			//request 12 addresses at a time, or whatever's left at the end
			if ((linecur != 0x0c) && (addr != maxaddr)) {
				continue;
			}

			unsigned curspeed, tmin, tsec;
			chron_cnt += linecur;
			chrono = diag_os_getms() - t0;
			if (chrono > 200) {
				//limit update rate
				curspeed = 1000 * (chron_cnt) / chrono; //avg B/s
				if (!curspeed) {
					curspeed += 1;
				}
				tsec = ((maxaddr - addr) / curspeed) % 9999;
				tmin = tsec / 60;
				tsec = tsec % 60;

				printf("\rreading @ 0x%08X (%3u %%, %5u B/s, ~ %3u:%02u remaining ", nextaddr,
				       (unsigned) 100 * (maxaddr - addr) / len, curspeed, tmin, tsec);
				fflush(stdout);
				chron_cnt = 0;
				t0 = diag_os_getms();
			}

			int i, rqok;
			//send the request "properly"

			rqok = diag_l2_send(global_l2_conn, &nisreq);
			if (rqok) {
				printf("\nhack mode : bad l2_send\n");
				retryscore -= 25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;  //out of for()
			}

			rqok=0; //default to fail

			//and get a response; we already know the max expected length:
			// 0xEC 0x81 + 2 (short hdr) or +4 (full hdr).
			// we'll request just 4 bytes so we return very fast;
			// We should find 0xEC if it's in there no matter what kind of header.
			// We'll "purge" the next bytes when we send SID 21
			errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
			                    hackbuf, 4, (unsigned) (25 + nparam_rxe.val));
			if (errval == 4) {
				//try to find 0xEC in the first bytes:
				for (i=0; i<=3; i++) {
					if (hackbuf[i] == 0xEC) {
						rqok=1;
						break;
					}
				}
			}

			if (!rqok) {
				printf("\nhack mode : bad AC response %02X %02X\n", hackbuf[0], hackbuf[1]);
				retryscore -= 25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;  //out of for()
			}
			//Here, we're guaranteed to have found 0xEC in the first 4 bytes we got. But we may
			//need to "purge" some extra bytes on the next read
			// hdr0 (hdr1) (hdr2) 0xEC 0x81 ck
			//
			extra = (3 + i - errval);   //bytes to purge. I think the formula is ok
			extra = (extra < 0) ? 0: extra; //make sure >=0

			//Here, we sent a AC 81 83 ... 83... request that was accepted.
			//We need to send 21 81 04 01 to get the data now
			txdata[0]=0x21;
			txdata[1]=0x81;
			txdata[2]=0x04;
			txdata[3]=0x01;
			nisreq.len=4;

			rqok=0; //default to fail
			//send the request "properly"
			if (diag_l2_send(global_l2_conn, &nisreq)) {
				printf("l2_send() problem !\n");
				retryscore -=25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;  //out of for ()
			}

			//and get a response; we already know the max expected length:
			//61 81 [2+linecur] + max 4 (header+cks) = 8+linecur
			//but depending on the previous message there may be extra
			//bytes still in buffer; we already calculated how many.
			//By requesting (extra) + 4 with a short timeout, we'll return
			//here very quickly and we're certain to "catch" 0x61.
			errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
			                    hackbuf, extra + 4, (unsigned) (25 + nparam_rxe.val));
			if (errval != extra+4) {
				retryscore -=25;
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				break;  //out of for ()
			}
			//try to find 0x61 in the first bytes:
			for (i=0; i<errval; i++) {
				if (hackbuf[i] == 0x61) {
					rqok=1;
					break;
				}
			}
			//we now know where the real data starts so we can request the
			//exact number of bytes remaining. Now, (errval - i) is the number
			//of packet bytes already read including 0x61, ex.:
			//[XX XX 61 81 YY YY ..] : i=2 and errval =5 means we have (5-2)=3 bytes
			// of packet data (61 81 YY)
			// Total we need (2 + linecur) packet bytes + 1 cksum
			// So we need to read (2+linecur+1) - (errval-i) bytes...
			// Plus : we need to dump those at the end of what we already got !
			extra = (3 + linecur) - (errval - i);
			if (extra<0) {
				printf("\nhack mode : problem ! extra=%d\n",extra);
				extra=0;
			} else {
				errval=diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
				                    &hackbuf[errval], extra, (unsigned) (25 + nparam_rxe.val));
			}

			if (errval != extra) {  //this should always fit...
				rqok=0;
			}

			if (!rqok) {
				//either negative response or not enough data !
				printf("\nhack mode : bad 61 response %02X %02X, i=%02X extra=%02X ev=%02X\n",
				       hackbuf[i], hackbuf[i+1], i, extra, errval);
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				retryscore -= 25;
				break;  //out of for ()
			}
			//and verify checksum. [i] points to 0x61;
			if (hackbuf[i+2+linecur] != diag_cks1(&hackbuf[i-1], 3+linecur)) {
				//this checksum will not work with long headers...
				printf("\nhack mode : bad 61 CS ! got %02X\n", hackbuf[i+2+linecur]);
				diag_data_dump(stdout, &hackbuf[i], linecur+3);
				diag_os_millisleep(300);
				(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
				retryscore -=20;
				break;  //out of for ()
			}

			//We can now dump this to the file...
			if (fwrite(&(hackbuf[i+2]), 1, linecur, outf) != linecur) {
				printf("Error writing file!\n");
				retryscore -= 101;  //fatal, sir
				break;  //out of for ()
			}

			nextaddr += linecur;    //if we crash, we can resume starting at nextaddr
			linecur=0;
			//success: allow us more errors
			retryscore = (retryscore > 95)? 100:(retryscore+5);

			//and reset tx template + sub-counters
			txdata[0]=0xAc;
			txdata[1]=0x81;
			nisreq.len=2;
			txi=2;

		}   //for
		if (addr <= maxaddr) {
			//the for loop didn't complete;
			//(if succesful, addr == maxaddr+1 !!)
			printf("\nRetry score: %d\n", retryscore);
		} else {
			printf("\nFinished! ~%lu Bps\n", 1000*(maxaddr - start)/(diag_os_getms() - total_chron));
			break;  //leave while()
		}
	}   //while retryscore>0

	if (retryscore <= 0) {
		printf("Too many errors, no more retries @ addr=%08X.\n", start);
		return CMD_FAILED;
	}
	return CMD_OK;
}


/** Read bytes from memory
 * copies <len> bytes from offset <addr> in ROM to *dest,
 * using SID AC and std L2_request mechanism.
 * Uses global conn, assumes global state is OK
 * @return num of bytes read
 */
static uint32_t read_ac(uint8_t *dest, uint32_t addr, uint32_t len) {
	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;
	uint32_t sent;  //count
	uint32_t goodbytes;

	if (!dest || (len==0)) {
		return 0;
	}

	unsigned int linecur;   //count from 0 to 11 (12 addresses per request)

	int txi;    //index into txbuf for constructing request

	txdata[0]=0xAC;
	txdata[1]=0x81;
	nisreq.len = 2; //AC 81 : 2 bytes so far
	nisreq.data=txdata;
	txi=2;
	linecur = 0;
	goodbytes = 0;
	for (sent=0; sent < len; addr++) {
		txdata[txi++]= 0x83;        //field type
		txdata[txi++]= (uint8_t) (addr >> 24) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 16) & 0xFF;
		txdata[txi++]= (uint8_t) (addr >> 8) & 0xFF;
		txdata[txi++]= (uint8_t) (addr & 0xFF);
		nisreq.len += 5;
		linecur += 1;
		sent++;
		//request 12 addresses at a time, or whatever's left at the end
		if ((linecur != 0x0c) && (sent != len)) {
			continue;
		}

		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL) {
			printf("\nError: no resp to rqst AC @ %08X, err=%d\n", addr, errval);
			break;  //leave for loop
		}
		if ((rxmsg->data[0] != 0xEC) || (rxmsg->len != 2) ||
		    (rxmsg->fmt & DIAG_FMT_BADCS)) {
			printf("\nFatal : bad AC resp at addr=0x%X:\n", addr);
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			diag_freemsg(rxmsg);
			break;
		}
		diag_freemsg(rxmsg);

		//Here, we sent a AC 81 83 ... 83... request that was accepted.
		//We need to send 21 81 04 01 to get the data now
		txdata[0]=0x21;
		txdata[1]=0x81;
		txdata[2]=0x04;
		txdata[3]=0x01;
		nisreq.len=4;

		rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg==NULL) {
			printf("\nFatal : did not get response at address %08X, err=%d\n", addr, errval);
			break;  //leave for loop
		}
		if ((rxmsg->data[0] != 0x61) || (rxmsg->len != (2+linecur)) ||
		    (rxmsg->fmt & DIAG_FMT_BADCS)) {
			printf("\nFatal : error at addr=0x%X: %02X, len=%u\n", addr,
			       rxmsg->data[0], rxmsg->len);
			diag_freemsg(rxmsg);
			break;
		}
		//Now we got the reply to SID 21 : 61 81 x x x ...
		memcpy(dest, &(rxmsg->data[2]), linecur);
		dest = &dest[linecur];
		goodbytes = sent;

		diag_freemsg(rxmsg);

		linecur=0;

		//and reset tx template + sub-counters
		txdata[0]=0xAc;
		txdata[1]=0x81;
		nisreq.len=2;
		txi=2;

	}   //for

	return goodbytes;
}

//(WIP) watch 4 bytes @ specified addr, using SID AC.
//
int cmd_watch(int argc, char **argv) {
	uint32_t addr;
	uint32_t len;
	uint8_t wbuf[4];

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (npstate == NP_DISC) {
		printf("Please connect first (\"nc\")\n");
		return CMD_FAILED;
	}

	addr = (uint32_t) htoi(argv[1]);
	printf("\nMonitoring 0x%0X; press Enter to interrupt.\n", addr);
	(void) diag_os_ipending();  //must be done outside the loop first
	while ( !diag_os_ipending()) {

		if (npstate == NP_NORMALCONN) {
			len = read_ac(wbuf, addr, 4);
			if (len != 4) {
				printf("? got %u bytes\n", len);
				return CMD_FAILED;
			}
		} else if (npstate == NP_NPKCONN) {
			if (npk_RMBA(wbuf, addr, 4)) {
				printf("RMBA problem\n");
				return CMD_FAILED;
			}
		}

		printf("\r0x%0X: %02X %02X %02X %02X", addr, wbuf[0], wbuf[1], wbuf[2], wbuf[3]);
		fflush(stdout);
	}
	printf("\n");
	return CMD_OK;
}

/** encrypt buffer in-place
 * @param len (count in bytes) is trimmed to align on 4-byte boundary, i.e. len=7 => len =4
 *
 * @return 16-bit checksum of buffer prior to encryption
 *
 */
static uint16_t encrypt_buf(uint8_t *buf, uint32_t len, uint32_t key) {
	uint16_t cks;
	if (!buf || !len) {
		return 0;
	}

	len &= ~3;
	cks = 0;
	for (; len > 0; len -= 4) {
		uint8_t tempbuf[4];
		memcpy(tempbuf, buf, 4);
		cks += tempbuf[0];
		cks += tempbuf[1];
		cks += tempbuf[2];
		cks += tempbuf[3];
		genkey1(tempbuf, key, buf);
		buf += 4;
	}
	return cks;
}


/** For Subaru, encrypt buffer in-place
 * @param len (count in bytes) is trimmed to align on 4-byte boundary, i.e. len=7 => len =4
 *
 * @return 16-bit checksum of buffer prior to encryption
 *
 */
void sub_encrypt_buf(uint8_t *buf, uint32_t len) {
	if (!buf || !len) {
		return;
	}

	len &= ~3;
	for (; len > 0; len -= 4) {
		uint8_t tempbuf[4];
		memcpy(tempbuf, buf, 4);
		sub_encrypt(tempbuf, buf);
		buf += 4;
	}
}


/** search for the given sid27 key in the known keysets;
 * if found, update the keyset.
 *
 * @return 1 if ok
 */
static bool set_keyset(u32 s27k) {
	unsigned i;
	for (i=0; known_keys[i].s27k != 0; i++) {
		if (s27k == known_keys[i].s27k) {
			nisecu.keyset = &known_keys[i];
			return 1;
		}
	}
	return 0;
}

#define S27K_DEFAULTADDR    0xffff8416UL
#define S27K_SEARCHSTART    0xffff8000UL
#define S27K_SEARCHEND  0xffffA000UL    //on 7055, 7058 targets this will be adequate. TODO : adjust according to nisecu.flashdev ?
#define S27K_SEARCHSIZE 0x80    //search this many bytes at a time

/* attempt to extract sid27 key by dumping RAM progressively. */
int cmd_guesskey(int argc, char **argv) {
	(void) argc;
	(void) argv;
	u8 buf[S27K_SEARCHSIZE];
	u32 test32;
	u32 maybe_8416;
	u32 foundaddr;
	const struct keyset_t *gkeyset;

	if (npstate != NP_NORMALCONN) {
		printf("Must be connected normally (nc command) !\n");
		return CMD_FAILED;
	}

	/* strategy : SID 27 01 to get seed, check @ ffff8416 first since that is very common.
	 * Else, fallback to dumping common area.
	 */

	uint8_t txdata[2] = {0x27, 0x01};
	struct diag_msg *rxmsg, nisreq={0};
	int errval;
	nisreq.len=2;
	nisreq.data=txdata;

	rxmsg=diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (rxmsg==NULL) {
		printf("couldn't 2701\n");
		return -1;
	}
	diag_freemsg(rxmsg);

	if (read_ac(buf, S27K_DEFAULTADDR, 4) != 4) {
		printf("short search failed?\n");
		return CMD_FAILED;
	}
	//on those ROMs that do this, the key is stored in bigendian at ffff8416.
	maybe_8416 = reconst_32(buf);

	if (set_keyset(maybe_8416)) {
		foundaddr = S27K_DEFAULTADDR;
		goto guesskey_found;
	}
	printf("Nothing @ 8416. Trying long search, press Enter to interrupt (may take a few seconds to interrupt)\n");
	// Assume the key, if present, is u16-aligned and saved in contiguous addresses.
	// i.e. not as two half-keys stored separately.
	// hence increment addr by (size - 2), to have overlapping chunks.

	(void) diag_os_ipending();  //must be done outside the loop first

	u32 addr;
	for (addr=S27K_SEARCHSTART; addr < S27K_SEARCHEND; addr += (S27K_SEARCHSIZE - 2)) {
		printf("%X\n",addr);
		if (read_ac(buf, addr, S27K_SEARCHSIZE) != S27K_SEARCHSIZE) {
			printf("long search query failed ?\n");
			return CMD_FAILED;
		}
		if (diag_os_ipending()) {
			break;
		}

		unsigned idx;
		for (idx=0; idx <= (S27K_SEARCHSIZE - 4); idx += 2) {
			//for every dumped u32, try to match against known keysets.
			test32 = reconst_32(&buf[idx]);
			if (set_keyset(test32)) {
				foundaddr = addr + idx;
				goto guesskey_found;
			}
		}
	}

	printf("key still not found. Maybe it's the one stored at ffff8416 anyway: 0x%08X ?\n"
	       "the sid36 key is still unknown though. Good luck.\n", (unsigned) maybe_8416);
	return CMD_FAILED;

guesskey_found:
	gkeyset = nisecu.keyset;
	printf("keyset %08X found @ 0x%08X and saved !\n", gkeyset->s27k, foundaddr);
	return CMD_OK;
}



#define KERNEL_MAXSIZE_SUB 8*1024U  //For SH7058, Subaru requires it to fit between 0xFFFF3000 and 0xFFFF5000

/* Does a complete SID 27 + 34 + 36 + 31 sequence to run the given kernel payload file.
 * Pads the input payload up to multiple of 4 bytes to make SID36 happy
 */
int cmd_sprunkernel(int argc, char **argv) {
	uint32_t file_len, pl_len, load_addr;
	FILE *fpl;
	uint8_t *pl_encr;   //encrypted payload buffer
	uint8_t cks_bypass[4] = { 0x00, 0x00, 0x5A, 0xA5 };  //required checksum
	struct diag_serial_settings set;
	int errval;

	if (argc != 2) {
		return CMD_USAGE;
	}

	const struct flashdev_t *fdt = nisecu.flashdev;
	if (!fdt) {
		printf("device type not set. Try \"setdev ?\"\n");
		return CMD_FAILED;
	}
	switch (fdt->mctype) {
	case SH7055:
		load_addr = 0xFFFF6000;
		break;
	case SH7058:
		load_addr = 0xFFFF3000;
		break;
	default:
		printf("For Subaru, kernel load and run only supported for SH7055S or SH7058\n");
		return CMD_FAILED;
	}

	if (npstate != NP_NORMALCONN) {
		printf("Must be connected normally (nc command) !\n");
		return CMD_FAILED;
	}

	if ((fpl = fopen(argv[1], "rb"))==NULL) {
		printf("Cannot open %s !\n", argv[1]);
		return CMD_FAILED;
	}

	file_len = flen(fpl);
	/* pad up to next multiple of 4 */
	pl_len = (file_len + 3) & ~3;
	printf("File Len %u Payload Len %u\n", file_len, pl_len);

	if (pl_len >= KERNEL_MAXSIZE_SUB) {
		printf( "***************** warning : large kernel detected *****************\n"
		        "That file seems way too big (%lu bytes) to be a typical kernel.\n"
		        "Trying anyway, but you might be using an invalid/corrupt file", (unsigned long) pl_len);
	}

	if (diag_malloc(&pl_encr, pl_len)) {
		printf("malloc prob\n");
		fclose(fpl);
		return CMD_FAILED;
	}

	if (fread(pl_encr, 1, file_len, fpl) != file_len) {
		printf("fread prob, file_len=%u\n", file_len);
		free(pl_encr);
		fclose(fpl);
		return CMD_FAILED;
	}

	if (file_len != pl_len) {
		printf("Using %u byte payload, padding with garbage to %u (0x0%X) bytes.\n", file_len, pl_len, pl_len);
	} else {
		printf("Using %u (0x0%X) byte payload.\n", file_len, file_len);
	}

	/* sid27 securityAccess */
	if (sub_sid27_unlock()) {
		printf("\nsid27 problem\n");
		goto badexit;
	}
	printf("\nsid27 done.\n");

	/* sid10 diagnosticSession */
	if (sub_sid10_diagsession()) {
		printf("\nsid10 problem\n");
		goto badexit;
	}
	printf("\nsid10 done.\n");

	// change speed to match Subaru BRR settings N = 29, so speed = 15,625 bit/s
	set.speed = 15625;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	update_params();

	errval=diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_SETSPEED, (void *) &set);
	if (errval) {
		printf("\nsprunkernel: could not setspeed\n");
		return -1;
	}

	/* sid34 requestDownload */
	if (sub_sid34_reqdownload(load_addr, pl_len)) {
		printf("\nsid34 problem for payload\n");
		goto badexit;
	}
	printf("\nsid34 done for payload.\n");

	/* encrypt payload */
	sub_encrypt_buf(pl_encr, (uint32_t) pl_len);

	/* sid36 transferData for payload */
	if (sub_sid36_transferdata(load_addr, pl_encr, (uint32_t) pl_len)) {
		printf("\nsid 36 problem for payload\n");
		goto badexit;
	}
	printf("sid36 done for payload.\n");

	/* sid34 requestDownload - checksum bypass put just after payload */
	if (sub_sid34_reqdownload((uint32_t) (load_addr + pl_len), 4)) {
		printf("\nsid34 problem for checksum bypass\n");
		goto badexit;
	}
	printf("\nsid34 done for checksum bypass.\n");

	sub_encrypt_buf(cks_bypass, (uint32_t) 4);

	/* sid36 transferData for checksum bypass */
	if (sub_sid36_transferdata((uint32_t) (load_addr + pl_len), cks_bypass, (uint32_t) 4)) {
		printf("\nsid 36 problem for checksum bypass\n");
		goto badexit;
	}
	printf("sid36 done for checksum bypass.\n");

	/* SID 37 TransferExit does not exist on all Subaru ROMs */

	/* RAMjump ! */
	if (sub_sid31_startRoutine()) {
		printf("sid 31 problem\n");
		goto badexit;
	}

	free(pl_encr);
	fclose(fpl);

	printf("SID 31 done.\nECU now running from RAM ! Disabling periodic keepalive;\n");

	if (npkern_init()) {
		printf("Problem starting kernel; try to disconnect + set speed + connect again.\n");
		return CMD_FAILED;
	}

	const char *npk_id;
	npk_id = get_npk_id();
	if (npk_id) {
		printf("Connected to kernel: %s\n", npk_id);
	}

	printf("You may now use kernel-specific commands.\n");
	npstate = NP_NPKCONN;

	return CMD_OK;

badexit:
	free(pl_encr);
	fclose(fpl);
	return CMD_FAILED;

}


#define KERNEL_MAXSIZE 10*1024U //warn for kernels larger than this

/* Does a complete SID 27 + 34 + 36 + BF sequence to run the given kernel payload file.
 * Pads the input payload up to multiple of 32 bytes to make SID36 happy
 */
int cmd_runkernel(int argc, char **argv) {
	const struct keyset_t *keyset;
	uint32_t sid27key;
	uint32_t sid36key;
	uint32_t file_len;
	uint32_t pl_len;

	FILE *fpl;
	uint8_t *pl_encr;   //encrypted payload buffer

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (npstate != NP_NORMALCONN) {
		printf("Must be connected normally (nc command) !\n");
		return CMD_FAILED;
	}

	keyset = nisecu.keyset;
	if (!keyset) {
		printf("No keyset selected - try \"setkeys\"\n");
		return CMD_FAILED;
	}

	sid27key = keyset->s27k;
	sid36key = keyset->s36k1;

	if ((fpl = fopen(argv[1], "rb"))==NULL) {
		printf("Cannot open %s !\n", argv[1]);
		return CMD_FAILED;
	}

	file_len = flen(fpl);
	/* pad up to next multiple of 32 */
	pl_len = (file_len + 31) & ~31;

	if (pl_len >= KERNEL_MAXSIZE) {
		printf( "***************** warning : large kernel detected *****************\n"
		        "That file seems way too big (%lu bytes) to be a typical kernel.\n"
		        "Trying anyway, but you might be using an invalid/corrupt file", (unsigned long) pl_len);
	}

	if (diag_malloc(&pl_encr, pl_len)) {
		printf("malloc prob\n");
		fclose(fpl);
		return CMD_FAILED;
	}

	if (fread(pl_encr, 1, file_len, fpl) != file_len) {
		printf("fread prob, file_len=%u\n", file_len);
		free(pl_encr);
		fclose(fpl);
		return CMD_FAILED;
	}

	if (file_len != pl_len) {
		printf("Using %u byte payload, padding with garbage to %u (0x0%X) bytes.\n", file_len, pl_len, pl_len);
	} else {
		printf("Using %u (0x0%X) byte payload.\n", file_len, file_len);
	}

	/* re-use NP 7 to get the SID27 done */
	if (sid27_unlock(1, sid27key)) {
		printf("sid27 problem\n");
		goto badexit;
	}

	/* SID 34 80 : */
	if (sid3480()) {
		printf("sid 34 80 problem\n");
		goto badexit;
	}
	printf("SID 34 80 done.\n");

	/* encrypt + send payload with SID 36 */
	uint16_t cks = 0;
	cks = encrypt_buf(pl_encr, (uint32_t) pl_len, sid36key);

	if (sid36(pl_encr, (uint32_t) pl_len)) {
		printf("sid 36 problem\n");
		goto badexit;
	}
	printf("SID 36 done.\n");

	/* SID 37 TransferExit */
	if (sid37(cks)) {
		printf("sid 37 problem\n");
		goto badexit;
	}
	printf("SID 37 done.\n");

	/* shit gets real here : RAMjump ! */
	if (sidBF()) {
		printf("RAMjump problem\n");
		goto badexit;
	}

	free(pl_encr);
	fclose(fpl);

	printf("SID BF done.\nECU now running from RAM ! Disabling periodic keepalive;\n");

	if (npkern_init()) {
		printf("Problem starting kernel; try to disconnect + set speed + connect again.\n");
		return CMD_FAILED;
	}

	const char *npk_id;
	npk_id = get_npk_id();
	if (npk_id) {
		printf("Connected to kernel: %s\n", npk_id);
	}

	printf("You may now use kernel-specific commands.\n");
	npstate = NP_NPKCONN;

	return CMD_OK;

badexit:
	free(pl_encr);
	fclose(fpl);
	return CMD_FAILED;
}


/** set speed + do startcomms, sabotage L2 modeflags for short headers etc.
 * Also disables keepalive
 * ret 0 if ok
 */
static int npkern_init(void) {
	struct diag_serial_settings set;
	struct diag_l2_14230 *dlproto;
	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;

	nisreq.data=txdata;

	/* Assume kernel is freshly booted : disable keepalive and setspeed */
	global_l2_conn->tinterval = -1;

	set.speed = (unsigned) nparam_kspeed.val;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	errval=diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_SETSPEED, (void *) &set);
	if (errval) {
		printf("npk_init: could not setspeed\n");
		return -1;
	}
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);

	dlproto = (struct diag_l2_14230 *)global_l2_conn->diag_l2_proto_data;
	dlproto->modeflags = ISO14230_SHORTHDR | ISO14230_LENBYTE | ISO14230_FMTLEN;

	/* StartComm */
	txdata[0] = 0x81;
	nisreq.len = 1;
	rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
	if (!rxmsg) {
		printf("npk_init: startcomm failed : %d\n", errval);
		return -1;
	}
	if (rxmsg->data[0] != 0xC1) {
		printf("npk_init: got bad startcomm response\n");
		diag_freemsg(rxmsg);
		return -1;
	}
	diag_freemsg(rxmsg);

	return 0;
}


/** npkernel SID 23 ReadMemoryByAddress
 * Supports addresses in two ranges :
 * [0 - 0x7F FFFF]	(bottom 8MB)
 * [0xFF80 0000 - 0xFFFF FFFF] (top 8MB)
 * that's 24 bits of addressing space of course.
 *
 * Assumes init was done before
 * ret 0 if ok
 */
static int npk_RMBA(uint8_t *dest, uint32_t addr, uint32_t len) {
	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	struct diag_msg *rxmsg=NULL;    //pointer to the reply
	int errval;

	nisreq.data=txdata;

	bool start_ROM = (addr < 0x800000);
	bool not_ROM = !start_ROM;
	bool start_RAM = (addr >= 0xFF800000);
	bool not_RAM = !start_RAM;

	if (((start_ROM) && ((addr + len) > 0x800000)) ||
	    (not_ROM && not_RAM)) {
		printf("npk RMBA addr out of bounds\n");
		return -1;
	}

	txdata[0] = SID_RMBA;
	nisreq.len = 5;

	while (len) {
		uint32_t curlen;
		txdata[1] = addr >> 16;
		txdata[2] = addr >> 8;
		txdata[3] = addr >> 0;
		curlen = len;
		if (curlen > 251) {
			curlen = 251;               //SID 23 limitation
		}
		txdata[4] = (uint8_t) curlen;

		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (!rxmsg) {
			printf("npk sid23 failed : %d\n", errval);
			return -1;
		}
		if ((rxmsg->data[0] != (SID_RMBA + 0x40)) || (rxmsg->len != curlen + 4)) {
			printf("got bad / incomplete SID23 response:\n");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
			return -1;
		}
		memcpy(dest, &rxmsg->data[1], curlen);
		diag_freemsg(rxmsg);
		len -= curlen;
		dest += curlen;
		addr += curlen;
	}
	return 0;

}


/** receive a bunch of dumpblocks (caller already send the dump request).
 * doesn't write the first "skip_start" bytes
 * ret 0 if ok
 */
static int npk_rxrawdump(uint8_t *dest, uint32_t skip_start, uint32_t numblocks) {
	uint8_t rxbuf[260];
	int errval;
	uint32_t bi;

	for(bi = 0; bi < numblocks; bi++) {
		//loop for every 32-byte response

		/* grab header. Assumes we only get "FMT PRC <data> cks" replies */
		errval = diag_l1_recv(global_l2_conn->diag_link->l2_dl0d,
		                      rxbuf, 3 + 32, (unsigned) (25 + nparam_rxe.val));
		if (errval < 0) {
			printf("dl1recv err\n");
			goto badexit;
		}
		uint8_t cks = diag_cks1(rxbuf, 2 + 32);
		if (    (errval != 35) ||
		        (rxbuf[0] != 0x21) ||
		        (rxbuf[1] != (SID_DUMP + 0x40)) ||
		        (cks != rxbuf[34])) {
			printf("no / incomplete / bad response\n");
			diag_data_dump(stdout, rxbuf, errval);
			printf("\n");
			goto badexit;
		}
		uint32_t datapos = 2;   //position inside rxbuf
		if (skip_start) {
			datapos += skip_start;  //because start addr wasn't aligned
			skip_start = 0;
		}

		uint32_t cplen = 34 - datapos;
		memcpy(dest, &rxbuf[datapos], cplen);
		dest += cplen;
	}   //for
	return 0;

badexit:
	return -1;
}

/** npkern-based fastdump (EEPROM / ROM / RAM)
 * kernel must be running first
 *
 * return 0 if ok. Caller must close fpl
 */
static int npk_dump(FILE *fpl, uint32_t start, uint32_t len, bool eep) {

	uint8_t txdata[64]; //data for nisreq
	struct diag_msg nisreq={0}; //request to send
	int errval;

	bool ram = 0;

	nisreq.data=txdata;

	if (start > 0xFF800000) {
		ram = 1;
	}

	if (ram && eep) {
		printf("bad args\n");
		return -1;
	}

	if (npkern_init()) {
		printf("npk init failed\n");
		goto badexit;
	}

	uint32_t skip_start = start & (32 - 1); //if unaligned, we'll be receiving this many extra bytes
	uint32_t iter_addr = start - skip_start;
	uint32_t willget = (skip_start + len + 31) & ~(32 - 1);
	uint32_t len_done = 0;  //total data written to file

	txdata[0] = SID_DUMP;
	txdata[1] = eep? SID_DUMP_EEPROM : SID_DUMP_ROM;
#define NP10_MAXBLKS    8   //# of blocks to request per loop. Too high might flood us
	nisreq.len = 6;

	unsigned t0 = diag_os_getms();

	while (willget) {
		uint8_t buf[NP10_MAXBLKS * 32];
		uint32_t numblocks;

		unsigned curspeed, tleft;
		unsigned long chrono;

		chrono = diag_os_getms() - t0;
		if (!chrono) {
			chrono += 1;
		}
		curspeed = 1000 * len_done / chrono;    //avg B/s
		if (!curspeed) {
			curspeed += 1;
		}
		tleft = (willget / curspeed) % 9999;    //s
		printf("\rnpk dump @ 0x%08X, %5u B/s, %4u s remaining\t", iter_addr, curspeed, tleft);
		fflush(stdout);

		numblocks = willget / 32;

		if (numblocks > NP10_MAXBLKS) {
			numblocks = NP10_MAXBLKS;                           //ceil

		}
		txdata[2] = numblocks >> 8;
		txdata[3] = numblocks >> 0;

		uint32_t curblock = (iter_addr / 32);
		txdata[4] = curblock >> 8;
		txdata[5] = curblock >> 0;

		if (ram) {
			errval = npk_RMBA(buf, iter_addr + skip_start, (numblocks * 32) - skip_start);
			if (errval) {
				printf("RMBA error!\n");
				goto badexit;
			}
		} else {
			errval = diag_l2_send(global_l2_conn, &nisreq);
			if (errval) {
				printf("l2_send error!\n");
				goto badexit;
			}
			if (npk_rxrawdump(buf, skip_start, numblocks)) {
				printf("rxrawdump failed\n");
				goto badexit;
			}
		}

		/* don't count skipped first bytes */
		uint32_t cplen = (numblocks * 32) - skip_start; //this is the actual # of valid bytes in buf[]
		skip_start = 0;

		/* and drop extra bytes at the end */
		uint32_t extrabytes = (cplen + len_done);   //hypothetical new length
		if (extrabytes > len) {
			cplen -= (extrabytes - len);
			//thus, (len_done + cplen) will not exceed len
		}
		uint32_t done = fwrite(buf, 1, cplen, fpl);
		if (done != cplen) {
			printf("fwrite error\n");
			goto badexit;
		}

		/* increment addr, len, etc */
		len_done += cplen;
		iter_addr += (numblocks * 32);
		willget -= (numblocks * 32);

	}   //while
	printf("\n");
	return 0;

badexit:
	(void) diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	fclose(fpl);
	return -1;
}


/* reflash a given block !
 */
int cmd_flblock(int argc, char **argv) {
	const struct flashdev_t *fdt = nisecu.flashdev;

	uint8_t *newdata;   //block data will be copied in this

	unsigned blockno;

	bool practice = 1;  //if set, disable modification to flash

	if ((argc < 3) || (argc > 4)) {
		return CMD_USAGE;
	}

	if (!fdt) {
		printf("device type not set. Try \"setdev ?\"\n");
		return CMD_FAILED;
	}

	if (npstate != NP_NPKCONN) {
		printf("kernel not initialized - try \"runkernel\" or \"initk\"\n");
		return CMD_FAILED;
	}

	blockno = (unsigned) htoi(argv[2]);

	if (blockno >= fdt->numblocks) {
		printf("block # out of range !\n");
		return CMD_FAILED;
	}

	newdata = load_rom(argv[1], fdt->romsize);

	if (!newdata) {
		return CMD_FAILED;
	}

	if (argc == 4) {
		if (argv[3][0] == 'Y') {
			printf("*** FLASH MAY BE MODIFIED ***\n");
		}
		(void) diag_os_ipending();  //must be done outside the loop first
		printf("*** Last chance : operation will be safely aborted in 3 seconds. ***\n"
		       "*** Press ENTER to MODIFY FLASH ***\n");
		diag_os_millisleep(3500);
		if (diag_os_ipending()) {
			printf("Proceeding with flash process.\n");
		} else {
			printf("Operation aborted; flash was not modified.\n");
			goto badexit;
		}
		practice = 0;
	} else {
		printf("*** Running in practice mode, flash will not be modified ***\n");
	}

	if (npkern_init()) {
		printf("npk init failed\n");
		goto badexit;
	}

	u32 bstart = fdt->fblocks[blockno].start;

	if (reflash_block(&newdata[bstart], fdt, blockno, practice) == CMD_OK) {
		printf("Reflash complete.\n");
		free(newdata);
		npkern_init();  //forces the kernel to disable write mode
		return CMD_OK;
	}

badexit:
	free(newdata);
	return CMD_FAILED;
}

int cmd_writevin(int argc, char** argv) {
#define VIN_LENGTH 17
	/*
		Possible To Do:
		- Could have it track what byte it's on when writing to give the user a progress visual
		- Could have it send $09 0x02 to verify that the VIN was sucessfully written (returns 4 messages)
	*/

	uint8_t txdata[4]; //data for nisreq
	struct diag_msg nisreq = { 0 }; //request to send
	struct diag_msg* rxmsg = NULL;    //pointer to the reply
	int errval;

	nisreq.data = txdata;

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (strlen(argv[1]) != VIN_LENGTH) {
		printf("VIN must be %u characters\n", VIN_LENGTH);
		return CMD_USAGE;
	}

	if (npstate != NP_NORMALCONN) {
		printf("Must be connected normally (nc command) !\n");
		return CMD_FAILED;
	}


	//  basically same as np_2(), but with a for loop around the diag_l2_request e.g.
	for (int idx = 0; idx < VIN_LENGTH; idx++)
	{
		txdata[0] = 0x3B;
		txdata[1] = idx + 0x04; // VIN writing starts at databyte 0x04
		txdata[2] = (int)toupper(argv[1][idx]);// Converts each character into an ASCII hex value [case insensitive]
		txdata[3] = 0x00; // 0x00 starts the write

		nisreq.len = 4;
		nisreq.data = txdata;


		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg == NULL) {
		}
		if ((rxmsg->data[0] != 0x7B) || (rxmsg->len != 2)) {
			printf("got bad 3B response : ");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
		}
		diag_freemsg(rxmsg);


		// Now repeat and send the complete write databyte 0xFF to finish the write
		txdata[0] = 0x3B;
		txdata[1] = idx + 0x04;
		txdata[2] = (int)toupper(argv[1][idx]);
		txdata[3] = 0xFF; // 0xFF completes the write

		nisreq.len = 4;
		nisreq.data = txdata;

		rxmsg = diag_l2_request(global_l2_conn, &nisreq, &errval);
		if (rxmsg == NULL) {
		}
		if ((rxmsg->data[0] != 0x7B) || (rxmsg->len != 2)) {
			printf("got bad 3B response : ");
			diag_data_dump(stdout, rxmsg->data, rxmsg->len);
			printf("\n");
			diag_freemsg(rxmsg);
		}
		diag_freemsg(rxmsg);
	}

	// Assume everything went smoothly (Add error handling logic later)
	printf("VIN writing complete.\n");
}


/* collection of numbered test functions - these are the old "np 9" etc functions.
 * stuff in here is doomed to either die or become a normal command
 */
int cmd_npt(int argc, char **argv) {
	unsigned testnum;

	uint32_t scode; //for SID27

	if ((argc <=1) || (sscanf(argv[1],"%u", &testnum) != 1)) {
		printf("Bad args\n");
		return CMD_USAGE;
	}

	if (global_state != STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_FAILED;
	}

	switch (testnum) {

	case 1:
		return np_1(argc, argv);
		break;
	case 2:
		return np_2(argc, argv);
		break;
	case 7:
		if (argc != 3) {
			printf("SID27 test. usage: npt 7 <scode>\n");
			return CMD_USAGE;
		}
		if ((sscanf(argv[2], "%x", &scode) != 1)) {
			printf("Did not understand %s\n", argv[2]);
			return CMD_USAGE;
		}
		return sid27_unlock(1, scode);
	case 6:
		return sid27_unlock(2, 0);
		break;  //case 6,7 (sid27)
	default:
		printf("test # invalid or deprecated\n");
		return CMD_USAGE;
		break;
	}   //switch testnum


	return CMD_OK;
}


/* kspeed <new_speed> */
int cmd_kspeed(int argc, char **argv) {

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (npstate != NP_NPKCONN) {
		printf("kernel not initialized - try \"runkernel\" or \"initk\"\n");
		return CMD_FAILED;
	}

	u16 newspeed = htoi(argv[1]);

	if (set_kernel_speed(newspeed)) {
		npkern_init();
		printf("Kernel did not accept new speed %ubps, try another speed or \"initk\"\n",
		       (unsigned) newspeed);
		return CMD_FAILED;
	}

	//the kernel has changed speed; now use npkern_init to update the serial port
	nparam_kspeed.val = (nparam_val) newspeed;

	if (npkern_init()) {
		printf("Failed to re-initialize kernel at new speed %ubps, try another speed or \"initk\"\n",
		       (unsigned) newspeed);
		diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
		return CMD_FAILED;
	}
	printf("Kernel now using %ubps.\n", (unsigned) newspeed);

	return CMD_OK;
}



/* flverif <file> */
int cmd_flverif(int argc, char **argv) {
	uint8_t *newdata;   //file will be copied to this
	const struct flashdev_t *fdt = nisecu.flashdev;
	bool *block_modified;

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (!fdt) {
		printf("device type not set. Try \"setdev ?\"\n");
		return CMD_FAILED;
	}

	if (npstate != NP_NPKCONN) {
		printf("kernel not initialized - try \"runkernel\" or \"initk\"\n");
		return CMD_FAILED;
	}

	newdata = load_rom(argv[1], fdt->romsize);
	if (!newdata) {
		return CMD_FAILED;
	}


	if (diag_calloc(&block_modified, fdt->numblocks)) {
		printf("malloc prob\n");
		goto badexit_nofree;
	}

	if (get_changed_blocks(newdata, NULL, fdt, block_modified)) {
		goto badexit;
	}

	printf("Different blocks : ");
	unsigned bcnt = 0;
	unsigned blockno;
	for (blockno = 0; blockno < fdt->numblocks; blockno++) {
		if (block_modified[blockno]) {
			printf("%u, ", blockno);
			bcnt += 1;
		}
	}
	printf("(total: %u)\n", bcnt);
	return CMD_OK;

badexit:
	free(block_modified);
badexit_nofree:
	free(newdata);
	return CMD_FAILED;

}

/* flrom <newrom> [<oldrom>] : flash whole ROM */
int cmd_flrom(int argc, char **argv) {
	uint8_t *newdata;   //file will be copied to this
	u8 *oldrom;

	const struct flashdev_t *fdt = nisecu.flashdev;
	bool *block_modified;

	if ((argc < 2) || (argc > 3)) {
		return CMD_USAGE;
	}

	if (!fdt) {
		printf("device type not set. Try \"setdev ?\"\n");
		return CMD_FAILED;
	}

	if (npstate != NP_NPKCONN) {
		printf("kernel not initialized - try \"runkernel\" or \"initk\"\n");
		return CMD_FAILED;
	}

	oldrom = NULL;
	if (argc == 3) {
		oldrom = load_rom(argv[2], fdt->romsize);
		if (!oldrom) {
			return CMD_FAILED;
		}
	}

	newdata = load_rom(argv[1], fdt->romsize);
	if (!newdata) {
		free(oldrom);
		return CMD_FAILED;
	}

	if (diag_calloc(&block_modified, fdt->numblocks)) {
		printf("malloc prob\n");
		goto badexit_nofree;
	}

	if (get_changed_blocks(newdata, oldrom, fdt, block_modified)) {
		goto badexit;
	}

	printf("Modified blocks : ");
	unsigned bcnt = 0;
	unsigned blockno;
	for (blockno = 0; blockno < fdt->numblocks; blockno++) {
		if (block_modified[blockno]) {
			printf("%u, ", blockno);
			bcnt += 1;
		}
	}
	printf("(total: %u)\n", bcnt);

	if (bcnt > (fdt->numblocks / 2)) {
		printf("\n*********** warning ***********\n"
				"More than half of the blocks have changes !\n");
	} else if (block_modified[0]) {
		printf("\n*********** warning ***********\n"
				"Block 0 (reset vectors and boot code) is modified. Proceed with care:\n"
				"flashing incorrect or incomplete data here could brick the ECU !\n");
	}

	printf("\n\ty : To reflash the blocks listed above, enter 'y'\n"
	       "\tf : to reflash the whole ROM\n"
	       "\tp : to do a dry run (practice mode) without modifying ROM contents\n"
	       "\tn : To abort/cancel, enter 'n'\n");

	char *inp = basic_get_input("> ", stdin);
	if (!inp) {
		//if user feeds an EOF, don't do anything
		goto badexit;
	}
	char answer=inp[0];
	free(inp);

	bool practice = 1;
	switch (answer) {
	case 'y':
		printf("reflashing selected blocks.\n");
		practice = 0;
		break;
	case 'p':
		printf("reflashing selected blocks (dry run). Note, some (harmless) write verification errors WILL\n"
		       "occur if there are \"modified blocks\" ! (i.e. ROM file differs from ECU ROM)\n");
		practice = 1;
		break;
	case 'f':
		/* set all blockflags as modified */
		for (blockno = 0; blockno < fdt->numblocks; blockno++) {
			block_modified[blockno] = 1;
		}
		printf("reflashing ALL blocks.\n");
		practice = 0;
		break;
	default:
		printf("Aborting.\n");
		goto goodexit;
		break;
	}

	for (blockno = 0; blockno < fdt->numblocks; blockno++) {
		u32 bstart;
		if (!block_modified[blockno]) {
			continue;
		}

		bstart = fdt->fblocks[blockno].start;
		printf("\tBlock %02u\n", blockno);
		if (reflash_block(&newdata[bstart], fdt, blockno, practice)) {
			goto badexit;
		}
	}

	printf("Reflash complete.\n");

goodexit:
	free(block_modified);
	free(newdata);
	free(oldrom);
	return CMD_OK;

badexit:
	free(block_modified);
badexit_nofree:
	free(newdata);
	free(oldrom);
	return CMD_FAILED;

}

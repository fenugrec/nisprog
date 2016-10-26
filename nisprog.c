/*
 *	nisprog - Nissan ECU communications utility
 *
 * Copyright (c) 2014-2016 fenugrec
 *
 * Licensed under GPLv3
 *
 * This standalone program compiles against components of freediag, and provides
 * Nissan-specific features aimed at dumping + reflashing ROMs.

 * This is experimental, and only tested on a handful of ECUs. So far it hasn't caused any permanent damage.

 * As-is, the code is of hack quality (low) and "trespasses" levels to go faster, skips some
 * checks, and is generally not robust. But it does work on a few different hardware setups and ECUs.
 */


//#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "diag_os.h"
#include "scantool_cli.h"

#include "nisprog.h"

FILE *dbg_stream=NULL;	//for nislib
enum npstate_t npstate;


#define NP_PROGNAME "nisprog"

const struct cmd_tbl_entry np_cmdtable[];

/*
 * Explain command line usage
 */
static void do_usage (void) {
	fprintf( stderr,	"nisprog utility for Nissan ECUs\n\n"
						"  Usage -\n"
						"	nisprog [-h] [-f <file]\n\n"
						"  Where:\n"
						"\t-h   -- Display this help message\n"
						"\t-f <file> Runs the commands from <file> at startup\n"
						"\n" ) ;
}

void nisecu_cleardata(struct nisecu_t *pne) {
	sprintf((char *) pne->ecuid, "UNK");
	pne->keyset = NULL;
	pne->flashdev = NULL;
	return;
}

/** ret 0 if ok */
static int np_init(void) {
	int rv;

	npstate = NP_DISC;
	nisecu_cleardata(&nisecu);

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		diag_end();
		return CMD_FAILED;
	}
	return 0;
}

int main(int argc, char **argv) {
	int i ;
	const char *startfile=NULL;	/* optional commands to run at startup */

	dbg_stream = tmpfile();
	if (!dbg_stream) {
		printf("can't create temp file !\n");
		goto badexit;
	}

	for ( i = 1 ; i < argc ; i++ ) {
		if ( argv[i][0] == '-' || argv[i][0] == '+' ) {
			switch ( argv[i][1] ) {
				case 'f' :
					i++;
					if (i < argc) {
						startfile = argv[i];
					} else {
						do_usage();
						goto badexit;
					}
					break;
				case 'h' : do_usage() ; goto goodexit;
				default : do_usage() ; goto badexit;
			}
		} else {
			do_usage() ;
			goto badexit;
		}
	}

	if (np_init()) {
		printf("Problem in np_init() !?\n");
		goto badexit;
	}

	enter_cli(NP_PROGNAME, startfile, np_cmdtable);

goodexit:
	(void) diag_end();
	if (dbg_stream) fclose(dbg_stream);
	return 0;

badexit:
	(void) diag_end;
	if (dbg_stream) fclose(dbg_stream);
	return 1;
}

const struct cmd_tbl_entry np_cmdtable[]=
{
	{ "nc", "nc", "Connect to ECU with current parameters",
			cmd_npconn, 0, NULL},
	{ "npdisc", "npdisc", "Disconnect from ECU (does not reset the ECU if running a kernel)",
			cmd_npdisc, 0, NULL},
	{ "npconf", "npconf <paramname> <value>", "Set some extra parameters",
			cmd_npconf, 0, NULL},
	{ "setdev", "setdev <device_no>", "Set mcu type",
			cmd_setdev, 0, NULL},
	{ "setkeys", "setkeys <sid27_key> [<sid36_key>]", "Set ECU keys. Specifying the SID 36 key is optional if the SID 27 key is a known keyset.\n"
													"Please consider submitting new keys to be added to the list !\n",
			cmd_setkeys, 0, NULL},
	{ "runkernel", "runkernel <file>", "Send + run specified kernel",
			cmd_runkernel, 0, NULL},
	{ "stopkernel", "stopkernel", "Disconnects + resets the ECU to exit kernel",
			cmd_stopkernel, 0, NULL},
	{ "watch", "watch <addr>", "Watch 4 bytes @ <addr>",
			cmd_watch, 0, NULL},
	{ "dumpmem", "dumpmem <file> <start> <#_of_bytes> [eep]", "(shorthand: \"dm\") dump memory from ROM/RAM address space, or EEPROM if\n"
							"\t\"eep\" is added at the end.\n"
							"\tExample: \"dm asdf.bin 0x1000 16\" will dump 0x10 bytes of ROM (0x1000-0x100F) to asdf.bin",
			cmd_dumpmem, 0, NULL},
	{ "dm", "dm <file> <start> <#_of_bytes> [eep]", "dump memory from ROM/RAM/EEPROM",
			cmd_dumpmem, FLAG_HIDDEN, NULL},
	{ "tfl", "tfl <file>", "[test] compare CRC of ROM to file",
			cmt_tfl, 0, NULL},
	{ "flblock", "flblock <romfile> <blockno> [Y]", "Reflash a single block from <romfile>. "
							"If 'Y' is absent, this runs in \"practice\" mode (without modifying flash ROM).\n"
							"ex.: \"flblock wholerom.bin 15 Y\"\n",
			cmd_flblock, 0, NULL},
	{ "npt", "npt [testnum]", "temporary / testing commands. Refer to source code",
			cmd_npt, 0, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};


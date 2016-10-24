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
	pne->ecutype = NISECU_UNK;
	memset(pne->ecuid, 'U', 5);
	pne->ecuid[5] = 0x00;
	pne->keyset = NULL;
	pne->fblock_descr = NULL;
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
	{ "disc", "disc", "Disconnect from ECU (without resetting the ECU if running a kernel)",
			cmd_npdisc, 0, NULL},
	{ "dumpmem", "dumpmem <file> <start> <#_of_bytes>", "(shorthand: \"dm\") dump memory from ROM/RAM address space\n"
							"\tExample: \"dm asdf.bin 0x1000 16\" will dump 0x10 bytes of ROM (0x1000-0x100F) to asdf.bin",
			cmd_dumpmem, 0, NULL},
	{ "dm", "dm <file> <start> <#_of_bytes>", "dump memory from ROM/RAM address space\n"
							"\tExample: \"dm asdf.bin 0x1000 16\" will dump 0x10 bytes of ROM (0x1000-0x100F) to asdf.bin",
			cmd_dumpmem, FLAG_HIDDEN, NULL},
	{ "npt", "npt [testnum]", "temporary / testing commands. Refer to source code",
			cmd_npt, 0, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};


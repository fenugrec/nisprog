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
#include "scantool_cli.h"

#include "nisprog.h"
#include "np_conf.h"

extern const char *GIT_REV; //defined in auto-generated version.c

FILE *dbg_stream=NULL;  //for nislib
enum npstate_t npstate;

const struct cmd_tbl_entry np_cmdtable[];

/*
 * Explain command line usage
 */
static void do_usage (void) {
	fprintf( stderr,    "nisprog utility for Nissan ECUs\n\n"
	         "  Usage -\n"
	         "	nisprog [-h] [-f <file]\n\n"
	         "  Where:\n"
	         "\t-h   -- Display this help message\n"
	         "\t-f <file> Runs the commands from <file> at startup\n"
	         "\n" );
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
	int i;
	const char *startfile=NULL; /* optional commands to run at startup */
	dbg_stream = stdout;

	for ( i = 1 ; i < argc ; i++ ) {
		if ( argv[i][0] == '-' || argv[i][0] == '+' ) {
			switch ( argv[i][1] ) {
			case 'f':
				i++;
				if (i < argc) {
					startfile = argv[i];
				} else {
					do_usage();
					goto badexit;
				}
				break;
			case 'h': do_usage(); goto goodexit;
			default: do_usage(); goto badexit;
			}
		} else {
			do_usage();
			goto badexit;
		}
	}

	if (np_init()) {
		printf("Problem in np_init() !?\n");
		goto badexit;
	}

	printf("\n**************** %s v%s-%s ****************\n", NP_PROGNAME, NP_VERSION, GIT_REV);
	printf("%s: Type HELP for a list of commands; \"debug ?\" to show debugging options.\n", NP_PROGNAME);
	printf("%s: **** IMPORTANT : this is beta software ! Use at your own risk.\n", SCANTOOL_PROGNAME);

	enter_cli(NP_PROGNAME, startfile, np_cmdtable);

goodexit:
	(void) diag_end();
	return 0;

badexit:
	(void) diag_end;
	return 1;
}

const struct cmd_tbl_entry np_cmdtable[] =
{
	{ "spconn", "spconn", "Connect to Subaru ECU with current parameters",
	  cmd_spconn, 0, NULL},
	{ "npconn", "npconn", "Connect to ECU with current parameters",
	  cmd_npconn, 0, NULL},
	{ "nc", "nc", "Connect to ECU with current parameters",
	  cmd_npconn, FLAG_HIDDEN, NULL},
	{ "npdisc", "npdisc", "Disconnect from ECU (does not reset the ECU if running a kernel)",
	  cmd_npdisc, 0, NULL},
	{ "nd", "nd", "Disconnect from ECU (does not reset the ECU if running a kernel)",
	  cmd_npdisc, FLAG_HIDDEN, NULL},
	{ "npconf", "npconf <paramname> <value>", "Set some extra parameters",
	  cmd_npconf, 0, NULL},
	{ "setdev", "setdev <device>", "Set mcu type",
	  cmd_setdev, 0, NULL},
	{ "gk", "gk", "Attempt to guess keyset",
	  cmd_guesskey, 0, NULL},
	{ "writevin", "writevin <vin>", "Writes the VIN to EEPROM.",
	  cmd_writevin, 0, NULL},
	{ "setkeys", "setkeys <sid27_key> [<sid36_key>]", "Set ECU keys. Specifying the SID 36 key is optional if the SID 27 key is a known keyset.\n"
	  "Please consider submitting new keys to be added to the list !\n",
	  cmd_setkeys, 0, NULL},
	{ "kspeed", "kspeed <new_speed>", "Change kernel comms speed and reinitialize kernel; Recommended <new_speed> values: 62500, 31250, 25000.",
	  cmd_kspeed, 0, NULL},
	{ "sprunkernel", "sprunkernel <file>", "Send + run specified kernel [Subaru]",
	  cmd_sprunkernel, 0, NULL},
	{ "runkernel", "runkernel <file>", "Send + run specified kernel [Nissan]",
	  cmd_runkernel, 0, NULL},
	{ "stopkernel", "stopkernel", "Disconnects + resets the ECU to exit kernel",
	  cmd_stopkernel, 0, NULL},
	{ "watch", "watch <addr>", "Watch 4 bytes @ <addr>",
	  cmd_watch, 0, NULL},
	{ "initk", "initk", "Initialize an already-running kernel",
	  cmd_initk, 0, NULL},
	{ "dumpmem", "dumpmem <file> <start> <#_of_bytes> [eep]", "(shorthand: \"dm\") dump memory from ROM/RAM address space, or EEPROM if\n"
	  "\t\"eep\" is added at the end.\n"
	  "\tExample: \"dm asdf.bin 0x1000 16\" : dump 16 bytes of ROM (0x1000-0x100F)\n"
	  "\tExample: \"dm asdf.bin 0 0\", specifying start and length as 0 will\n"
	  "\tdump the entire ROM (must run \"setdev\" first)\n",
	  cmd_dumpmem, 0, NULL},
	{ "dm", "dm <file> <start> <#_of_bytes> [eep]", "(see \"dumpmem\")",
	  cmd_dumpmem, FLAG_HIDDEN, NULL},
	{ "flverif", "flverif <file>", "Compare <file> against ROM",
	  cmd_flverif, 0, NULL},
	{ "flblock", "flblock <romfile> <blockno> [Y]", "Reflash a single block from <romfile>. "
	  "If 'Y' is absent, this runs in \"practice\" mode (without modifying flash ROM).\n"
	  "ex.: \"flblock wholerom.bin 15 Y\"\n",
	  cmd_flblock, 0, NULL},
	{ "flrom", "flrom <romfile> [<orig_rom>]", "Reflash a new ROM from <romfile>. "
	  "If <orig_rom> is specified, it is used to select which blocks to reflash instead of the normal CRC comparison.\n"
	  "ex.: \"flrom newrom.bin\"\n",
	  cmd_flrom, 0, NULL},
	{ "npt", "npt [testnum]", "temporary / testing commands. Refer to source code",
	  cmd_npt, 0, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};


/* nisprog interactive CLI */
/* copyright (c) fenugrec 2016
 * licensed under GPLv3
 */


//#include <stdlib.h>
#include <stdio.h>

#include "diag.h"
#include "diag_os.h"
#include "scantool_cli.h"

#include "nisprog.h"


#define NP_PROGNAME "nisprog"
const struct cmd_tbl_entry np_cmdtable[];

/* garbage to make freediag cli compile */
const struct cmd_tbl_entry test_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};
const struct cmd_tbl_entry vag_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};
const struct cmd_tbl_entry dyno_cmd_table[] = {{ NULL, NULL, NULL, NULL, 0, NULL}};

/*
 * Explain command line usage
 */
static void do_usage (void)
{
	fprintf( stderr,	"nisprog utility for Nissan ECUs\n\n"
						"  Usage -\n"
						"	scantool [-h] [-f <file]\n\n"
						"  Where:\n"
						"\t-h   -- Display this help message\n"
						"\t-f <file> Runs the commands from <file> at startup\n"
						"\n" ) ;
}


int do_init(void) {
	return 0;
}

int
main(int argc, char **argv)
{
	int i ;
	const char *startfile=NULL;	/* optional commands to run at startup */

	for ( i = 1 ; i < argc ; i++ ) {
		if ( argv[i][0] == '-' || argv[i][0] == '+' ) {
			switch ( argv[i][1] ) {
			case 'f' :
				i++;
				if (i < argc) {
					startfile = argv[i];
				} else {
					do_usage();
					return 1;
				}
				break;
			case 'h' : do_usage() ; return 0 ;
			default : do_usage() ; return 1 ;
			}
		} else {
			do_usage() ;
			return 1;
		}
	}

	/* Input buffer */

	do_init();

	enter_cli(NP_PROGNAME, startfile, np_cmdtable);

	/* Done */
	return 0;
}

int cmd_dumpmem(UNUSED(int argc), UNUSED(char **argv)) {
	return CMD_OK;
}



const struct cmd_tbl_entry np_cmdtable[]=
{
	{ "dm", "dumpmem", "dump memory from ROM/RAM address space",
		cmd_dumpmem, 0, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};


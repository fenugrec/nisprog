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


const char progname[]="nisprog";

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

	//enter_cli(progname, startfile);

	/* Done */
	return 0;
}



int
cmd_up(UNUSED(int argc), UNUSED(char **argv))
{
	return CMD_UP;
}


int
cmd_exit(UNUSED(int argc), UNUSED(char **argv))
{
	return CMD_EXIT;
}

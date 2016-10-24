#ifndef _NISPROG_H
#define _NISPROG_H

/* state of connection to ECU */
enum npstate_t {
	NP_DISC,	/** disconnected */
	NP_NORMALCONN, /** normal connection to stock firmware */
	NP_NPKCONN /** kernel connection to npkern */
};

extern enum npstate_t npstate;

int cmd_dumpmem(UNUSED(int argc), UNUSED(char **argv));
int cmd_npt(int argc, char **argv);

#endif

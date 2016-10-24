#ifndef _NISPROG_H
#define _NISPROG_H

#ifdef __GNUC__
	#define UNUSED(X) 	X __attribute__((unused))	//magic !
#else
	#define UNUSED(X)	X	//how can we suppress "unused parameter" warnings on other compilers?
#endif // __GNUC__


#endif

/*
 * Globulation 2 crash handling file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#if (!defined (__GNUC__)) && (!defined (WIN32))

#include <mcheck.h>
#include <signal.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include "Header.h"

/* Obtain a backtrace and print it to `stdout'. */
void printTrace (void)
{
	void *array[20];
	size_t size;
	char **strings;
	size_t i;

	size = backtrace (array, 20);
	strings = backtrace_symbols (array, size);

	printf ("Obtained %d stack frames :\n", (unsigned)size);

	for (i = 0; i < size; i++)
	printf ("%s\n", strings[i]);

	free (strings);
}

static void termination_handler (int sig)
{
	signal(sig, SIG_DFL);
	fprintf(stdout, "DBG : Oooops : caught fatal signal : ");
	switch (sig)
	{
		case SIGSEGV:
			fprintf(stdout, "Segmentation Fault");
			break;
		case SIGBUS:
			fprintf(stdout, "Bus Error");
			break;
		case SIGFPE:
			fprintf(stdout, "Floating Point Exception");
			break;
		case SIGQUIT:
			fprintf(stdout, "Keyboard Quit");
			break;
		case SIGPIPE:
			fprintf(stdout, "Broken Pipe");
			break;
		default:
			fprintf(stdout, "# %d", sig);
			break;
	}
	fprintf(stdout, "\n");
	fprintf(stdout, "Now cleaning static stuff :\n");
	SDL_Quit();
	exit(-sig);
}

static int fatal_signals[] = {
	SIGSEGV,
	SIGBUS,
	SIGFPE,
	SIGQUIT,
	SIGPIPE,
	0
};

void installCrashHandler(void)
{
	mtrace ();
	printf("DBG : Signal support enabled\n");
	{
		for (int i=0; fatal_signals[i]; ++i )
			signal(fatal_signals[i], termination_handler);
	}
}

#else

#include <stdio.h>

void installCrashHandler(void)
{
	printf("DBG : No crash support on this plateform\n");
}

void printTrace(void)
{
	printf("DBG : No stack trace support\n");
}

#endif

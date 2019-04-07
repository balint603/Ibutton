/*
 * Cron.
 */
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <paths.h>
#include <sys/times.h>

#include "cron.h"

char *pgm;

/* keep all the crontabs in a sorted-by-uid array, 
 */
crontab *tabs = 0;
int    nrtabs = 0;
int    sztabs = 0;

/** eat blakns */
char*
firstnonblank(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;

    return s;
}


/* SIGCHLD handler -- eat a child status, reset the signal handler, then
 * return.
 */
static void
eat(int sig)
{
    int status;

    waitpid(-1, &status, 0);
    signal(SIGCHLD,eat);
}

/* \brief Look the time.
 * \param t current time, or time
 * \param m mask
 * \ret 0 if the time DOES NOT fall inside the event mask
 * \ret 1 if the tim FALL inside the event mask
 */
int
check_domain(Evmask *t, Evmask *m)
{
    if ( ( (t->minutes[0] & m->minutes[0]) || (t->minutes[1] & m->minutes[1]) )
	&& (t->hours & m->hours)
	&& (t->mday & m->mday)
	&& (t->month & m->month)
	&& (t->wday & m->wday) ) return 1;
    return 0;
}

time_t ct_dirtime;


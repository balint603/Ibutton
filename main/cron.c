/**
 * \addtogroup Cron_module
 * @{
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

/** \brief Eat blanks. */
char* firstnonblank(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;

    return s;
}


/** \brief Compare the time masks.
 * \param t current time in binary form.
 * \param m mask in binary form.
 * \return 0 if the time DOES NOT fall inside the event mask.
 * \return 1 if the time FALL inside the event mask
 */
int check_domain(Evmask *t, Evmask *m) {
    if ( ( (t->minutes[0] & m->minutes[0]) || (t->minutes[1] & m->minutes[1]) )
	&& (t->hours & m->hours)
	&& (t->mday & m->mday)
	&& (t->month & m->month)
	&& (t->wday & m->wday) ) return 1;
    return 0;
}

/** @} */

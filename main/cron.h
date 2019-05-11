/**
 * @defgroup Cron_module
 * \brief Cron parser and time comparator module.
 *  Based on: https://github.com/Orc/cron
 *  This product includes software developed by
 * David Loren Parsons <http://www.pell.portland.or.us/~orc>
 * @{
 * */
#ifndef _CRON_H
#define _CRON_H

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

/** Separator between multiple crons. */
#define SEPARATOR ';'

#define CRON_MAX_SIZE 256
#define CBIT(t)		(1 << (t))


/**
 * 60 bits for minute,
 * 31 bits for mday,
 * 24 bits for hour,
 * 12 bits for month,
 *  7 bits for wday
 */
typedef struct {
	unsigned long minutes[2];	/* 60 bits worth 16B */
    unsigned long hours;	/* 24 bits worth 8B */
    unsigned long mday;		/* 31 bits worth 8B */
    unsigned int  wday;		/*  7 bits worth 4B */
    unsigned int  month;	/* 12 bits worth 4B */
} Evmask;

int check_domain(Evmask *t, Evmask *m);
char *getdatespec(char *cron_s, Evmask *time_mask);
void tmtoEvmask(struct tm *, Evmask*);
int checkcrons(char *crons_s, struct tm *time);

char *firstnonblank(char*);
void error(char*,...);
void fatal(char*,...);

extern int interactive;
extern int lineno;

#endif
/** @} */

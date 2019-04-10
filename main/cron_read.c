/*
 * Cron parser.
 */
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include "esp_log.h"
#include "cron.h"

#define CRON_TEST

#define TAG "CRON"

typedef void (*ef)(Evmask*,int);

/* a constraint defines part of a time spec.
 * min & max are the smallest and largest possible values,
 * units is a string describing what the constraint is,
 * setter is a function that sets the time spec.
 */
typedef struct {
    int min;
    int max;
    char *units;
    ef setter;
} constraint;

static void
sminute(Evmask *mask, int min)
{
    if (min >= 32)
	mask->minutes[1] |= 1 << (min-32);
    else
	mask->minutes[0] |= 1 << (min);
}

static void
shour(Evmask *mask, int hour)
{
    mask->hours |= 1 << (hour);
}

static void
smday(Evmask *mask, int mday)
{
    mask->mday |= 1 << (mday);
}

static void
smonth(Evmask *mask, int month)
{
    mask->month |= 1 << (month);
}

static void
swday(Evmask *mask, int wday)
{
    if (wday == 0) wday = 7;
    mask->wday |= 1 << (wday);
}

static constraint minutes = { 0, 59, "minutes", sminute };
static constraint hours =   { 0, 23, "hours", shour };
static constraint mday =    { 0, 31, "day of the month", smday };
static constraint months =  { 1, 12, "months",  smonth };
static constraint wday =    { 0,  7, "day of the week", swday };


/* Check a number against a constraint.
 */
static int
constrain(int num, constraint *limit)
{
    return (num >= limit->min) || (num <= limit->max);
}


static int logicalline;


/* Pick a number off the front of a string, validate it,
 * and repoint the string to after the number.
 */
static int
number(char **s, constraint *limit)
{
    int num;
    char *e;

    num = strtoul(*s, &e, 10);
    if (e == *s) {
    	return 0;
    }

    if (limit) {
	if (constrain(num, limit) == 0) {
	    return 0;
	}
    }
    *s = e;
    return num;
}


/* Assign a value to an Evmask.
 */
static void
assign(int time, Evmask *mask, ef setter)
{
    (*setter)(mask, time);
}


/* \brief Parsing the cron string to a time mask
 * Pick a time field off a line, returning a pointer to
 * the rest of the line.
 */
static char *
parse(char *s, Evmask *time_mask, constraint *limit)
{
    int num, num2, skip;

    if (s == 0)
	return 0;

    do {
	skip = 0;
	num2 = 0;

	if (*s == '*') {
	    num = limit->min;
	    num2 = limit->max;
	    ++s;
	}
	else {
	    num = number(&s,limit);

	    if (*s == '-') {
		++s;
		num2 = number(&s, limit);
		skip = 1;
	    }
	}

	if ( *s == '/' ) {
	    ++s;
	    skip = number(&s, 0);
	}

	if (num2) {
	    if (skip == 0) skip = 1;
	    while ( constrain(num, limit) && (num <= num2) ) {
		assign(num, time_mask, limit->setter);
		num += skip;
	    }
	}
	else
	    assign(num, time_mask, limit->setter);

	if ( isspace((unsigned char)*s) ) return firstnonblank(s);
	else if (*s != ',') 
	    return 0;
	++s;
    } while (1);
}


/* \brief Create a mask type data from cron string.
 * Read the entire time spec off the start of a line.
 * \param cron_s cron string
 * \param time_mask result output data
 * \ret either null (an error) or a pointer to
 * the rest of the line.
 */
char *
getdatespec(char *cron_s, Evmask *time_mask)
{
    bzero(time_mask, sizeof *time_mask);
    cron_s = parse(cron_s, time_mask, &minutes);
    cron_s = parse(cron_s, time_mask, &hours);
    cron_s = parse(cron_s, time_mask, &mday);
    cron_s = parse(cron_s, time_mask, &months);
    cron_s = parse(cron_s, time_mask, &wday);
    return cron_s;
}


/* \brief Convert a time <time.h> into an event mask.
 * \param tm will be converted to time.
 * \param time output of the conversion
 */
void
tmtoEvmask(struct tm *tm, Evmask *time)
{
    bzero(time, sizeof *time);

    sminute(time, tm->tm_min);
    shour(time,   tm->tm_hour);
    smday(time,   tm->tm_mday);
    smonth(time,1+tm->tm_mon);
    swday(time,   tm->tm_wday);
}

char *
split_crons(char *crons_s, size_t *length){
	if(crons_s == NULL)
		return NULL;

	char *cron_next = crons_s;
	while((*length)--){
		switch(*cron_next){
			case '\0':
				ESP_LOGD(TAG,"EOF");
				return NULL; 	// It was the last cron string, (EOF) there are no next.
				break;
			case SEPARATOR:
				*cron_next = '\0';	// Change it to NULL terminator
				ESP_LOGD(TAG,"SEPARATOR");
				return ++cron_next;	// Must point to the next cron str
				break;
			default:
				break;
		}
		cron_next++;
	}
	ESP_LOGD(TAG,"SIZE OUT");
	return NULL;	// No null terminator in size range.
}

/** \brief Check all crons.
 *  \param crons_s Cron strings with separators.
 *    - 'OR' operation between cron strings.
 *  \ret 0 out of domains
 *  \ret 1 in a domain
 * */
int
checkcrons(char *crons_s, struct tm *time)
{
	char cron[256];
	char *cron_next;
	char *cron_cur = cron;
	size_t cron_length = CRON_MAX_SIZE-1;
	Evmask mask_time;
	Evmask mask_cron;

	if(crons_s == NULL)	// No crons.
		return 1;
#ifdef CRON_TEST
	ESP_LOGD(TAG,"Got crons:[%s]\n",crons_s);
	ESP_LOGD(TAG,"Got time: %i,%i,%i,%i,%i\n",time->tm_min, time->tm_hour,
			time->tm_mday, time->tm_mon, time->tm_wday);
#endif
	tmtoEvmask(time, &mask_time);	// Convert tm struct into mask
	strcpy(cron,crons_s);
	while(cron_length > 0){
		cron_next = split_crons(cron_cur, &cron_length);
#ifdef CRON_TEST
		ESP_LOGD(TAG,"cron_next=%s\n",cron_next == NULL ? "NULL" : cron_next);
		ESP_LOGD(TAG,"Check this cron=[%s]\n",cron_cur == NULL ? "NULL" : cron_cur);
#endif
		getdatespec(cron_cur, &mask_cron);	// Convert string into mask
		if(check_domain(&mask_time, &mask_cron))
			return 1;									// In domain
		if(cron_next == NULL)
			return 0;	// End of domain / Null parameter

		cron_cur = cron_next;
	}
	return 0;	// Not found a domain fitted in
}
























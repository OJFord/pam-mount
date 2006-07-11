/*=============================================================================
common.c
  Copyright (C) W. Michael Putello <new@flyn.org>, 1999
  Copyright © Jan Engelhardt <jengelh [at] gmx de>, 2005 - 2006

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of
  the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program; if not, write to:
  Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
  Boston, MA  02110-1301  USA

  -- For details, see the file named "LICENSE.LGPL2"
=============================================================================*/
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "fmt_ptrn.h"

static char *_firstname(void);
static char *_lastname(void);
static char *_middlename(void);
static char *day(char *, size_t);
static char *month(char *, size_t);
static int parse_kv(char *, char **, char **);
static void shift_str(char *, char *);
static char *year(char *, size_t);

extern char **environ;

/* FIXME: the code in these functions needs to be checked for:
 * 1.  a consistent interface for memory management
 * 2.  memory leaks
 * 3.  use of g_free/g_strdup/etc. instead of free/strdup/etc.
 * 4.  does (ie) g_get_real_name() ever return NULL and is this a problem
 *     for g_strdup?
 */

/* ============================ firstname () ================================ */
static char *_firstname(void)
{
	char *name, *ptr;
	if((name = g_strdup(g_get_real_name())) == NULL)
		return NULL;
	if((ptr = strchr(name, ' ')) != NULL)
		*ptr = '\0';
	return name;
}

/* ============================ shift_str () =============================== */
static void shift_str (char *ptr_0, char *ptr_1)
{
    /* This strcpy()-like function supports _overlapping_ strings.
    Libc does not [explicitly]. */
    while(*ptr_1 != '\0')
        *ptr_0++ = *ptr_1++;
    *ptr_0 = '\0';
}

/* ============================ middlename () =============================== */
static char *_middlename(void)
{
	char *name, *a, *b;
	if((name = g_strdup(g_get_real_name())) == NULL)
		return NULL;
	if((a = strchr(name, ' ')) == NULL)
		return NULL;
	if((b = strchr(++a, ' ')) == NULL)
		return NULL;
	*b = '\0';
	shift_str(name, a);
        return name;
}

/* ============================ lastname () =============================== */
static char *_lastname(void)
{
	char *name, *a, *b;
	if((name = g_strdup(g_get_real_name())) == NULL)
		return NULL;
	if((a = strchr(name, ' ')) == NULL)
		return NULL;
	if((b = strchr(++a, ' ')) == NULL)
		return a;
	shift_str(name, ++b);
	return name;
}

static char *day(char *d, size_t s) {
    time_t sec_since_1970;
    struct tm *curr_time;

    *d = '\0';
    time(&sec_since_1970);
    curr_time = localtime(&sec_since_1970);
    strftime(d, s, "%d", curr_time);

    return d;
}

static char *month(char *m, size_t s) {
    time_t sec_since_1970;
    struct tm *curr_time;

    *m = '\0';
    time(&sec_since_1970);
    curr_time = localtime(&sec_since_1970);
    strftime(m, s, "%B", curr_time);

    return m;
}

static char *year(char *y, size_t s) {
    time_t sec_since_1970;
    struct tm *curr_time;

    *y = '\0';
    time(&sec_since_1970);
    curr_time = localtime(&sec_since_1970);
    strftime(y, s, "%Y", curr_time);

    return y;
}

/* ============================ print_dir () ================================ */
void print_dir(DIR * dp)
{
    struct dirent *ent;
    while((ent = readdir(dp)) != NULL) {
	if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
	    continue;
	printf("  %s\n", ent->d_name);
    }
}

/* ============================= initialize_fillers_from_file () ============ */
void initialize_fillers_from_file(fmt_ptrn_t *x, char *path)
{
    char line[PATH_MAX + 1], *key, *value, *ptr = line;
    FILE *input = fopen (path, "r");

    while(fgets(ptr, sizeof(line), input) != NULL) {
        key = strsep(&ptr, "=");
	value = ptr;
        fmt_ptrn_update_kv(x, g_strdup(key), g_strdup(value));
    }
}

/* ============================= initialize_fillers () ====================== */
void initialize_fillers(fmt_ptrn_t *x)
{
    int i;
    char b[BUFSIZ + 1], *key, *val;
    for(i = 0; environ[i] != NULL; i++)
        if(environ[i] != NULL && parse_kv(environ[i], &key, &val))
	    fmt_ptrn_update_kv(x, key, val);
    fmt_ptrn_update_kv(x, g_strdup("DAY"), g_strdup(day(b, sizeof(b))));
    fmt_ptrn_update_kv(x, g_strdup("MONTH"), g_strdup(month(b, sizeof(b))));
    fmt_ptrn_update_kv(x, g_strdup("YEAR"), g_strdup(year(b, sizeof(b))));
    fmt_ptrn_update_kv(x, g_strdup("FULLNAME"), g_strdup(g_get_real_name()));
    fmt_ptrn_update_kv(x, g_strdup("FIRSTNAME"), _firstname());
    fmt_ptrn_update_kv(x, g_strdup("MIDDLENAME"), _middlename());
    fmt_ptrn_update_kv(x, g_strdup("LASTNAME"), _lastname());
    fmt_ptrn_update_kv(x, g_strdup("EMPTY_STR"), g_strdup(""));
}

static int parse_kv(char *str, char **key, char **val) {
    char *tmp = strdup(str), *wp = tmp;
    *key = strdup(strsep(&wp, "="));
    *val = strdup((wp != NULL) ? wp : "");
    free(tmp);
    return 1;
}

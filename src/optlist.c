/*=============================================================================
optlist.c
  Copyright (C) W. Michael Putello <mike@flyn.org>, 2003
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
#include <assert.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "optlist.h"
#include "pair.h"
#include "private.h"

static int _compare(gconstpointer, gconstpointer);
static int _parse_opt(const char *, size_t, optlist_t **);
static int _parse_string_opt(const char *str, size_t, optlist_t **);

/* ============================ _parse_string_opt () ======================= */
/* INPUT: str, string to parse
 *        len, should be length up to first ',' or terminating '\0'
 * SIDE EFFECTS: str[0 - len] has been parsed and placed in optlist
 * OUTPUT: if error 0 else 1
 */
static int _parse_string_opt(const char *str, size_t len,
			     optlist_t ** optlist)
{
	int ret = 1;
	struct pair *pair;
	char *delim, *key, *val;

	assert(str != NULL);
	/* a user could config "loop,,,foo=bar==..." */
	if (len <= 0 || len > MAX_PAR) {
		ret = 0;
		goto _return;
	}
	assert(len > 0 && len <= strlen(str) && len <= MAX_PAR);
	assert(optlist != NULL);

	delim = strchr(str, '=');
	if(delim == NULL || delim - str >= len) {
		ret = 0;
		goto _return;
	}
	pair = g_new0(struct pair, 1);
	key = g_new0(char, delim - str + 1);
	val = g_new0(char, len - (delim - str));	/* '=' is +1 */
	strncpy(key, str, delim - str);
	key[delim - str] = '\0';
	strncpy(val, delim + 1, len - (delim - str) - 1);
	val[len - (delim - str) - 1] = '\0';
	pair_init(pair, key, val, g_free, g_free);
	*optlist = g_list_append(*optlist, pair);
      _return:

	assert(!ret || (optlist_exists(*optlist, key)
			&& strcmp(optlist_value(*optlist, key), val) == 0));

	return ret;
}

/* ============================ _parse_opt () ============================== */
/* INPUT: str, string to parse
 *        len, should be length up to first ',' or terminating '\0'
 * SIDE EFFECTS: str[0 - len] has been parsed and placed in optlist
 * OUTPUT: if error 0 else 1
 */
static int _parse_opt(const char *str, size_t len, optlist_t ** optlist)
{
	int ret = 1;
	struct pair *pair;
	char *key, *val;

	assert(str != NULL);
	/* a user could config "loop,,,foo=bar==..." */
	if (len <= 0 || len > MAX_PAR) {
		ret = 0;
		goto _return;
	}
	assert(len > 0 && len <= strlen(str) && len <= MAX_PAR);
	assert(optlist != NULL);

	pair = g_new0(struct pair, 1);
	key = g_new0(char, len + 1);
	val = g_new0(char, 1);
	strncpy(key, str, len);
	key[len] = '\0';
	*val = '\0';
	pair_init(pair, key, val, g_free, g_free);
	*optlist = g_list_append(*optlist, pair);
      _return:

	assert(!ret || (optlist_exists(*optlist, key)
			&& !strcmp(optlist_value(*optlist, key), val)));

	return ret;
}

/* ============================ str_to_optlist () ========================== */
/* INPUT: str, string to parse
 * SIDE EFFECTS: str has been parsed and placed in optlist
 * OUTPUT: if error 0 else 1
 */
gboolean str_to_optlist(optlist_t ** optlist, const char *str)
{
	int ret = 1;
	char *ptr;

	assert(optlist != NULL);
	assert(str != NULL);

	*optlist = NULL;
	if(strlen(str) == 0) {
		ret = 0;
		goto _return;
	}
	while ((ptr = strchr(str, ',')) != NULL) {
            if(!_parse_string_opt(str, ptr - str, optlist) &&
              !_parse_opt(str, ptr - str, optlist)) {
                    ret = 0;
                    goto _return;
            }
            str = ptr + 1;
	}
        if(!_parse_string_opt(str, strlen(str), optlist) &&
          !_parse_opt(str, strlen(str), optlist)) {
                ret = 0;
                goto _return;
        }
      _return:

	assert(!ret || ((strlen(str) == 0 && *optlist == '\0') || *optlist != '\0'));

	return ret;
}

/* ============================ _compare () ================================ */
/* INPUT: x and y
 * OUTPUT: if x->key is the same string as y then 0, else non-0
 */
static int _compare(gconstpointer x, gconstpointer y)
{
        const struct pair *px = x;
	assert(x != NULL);
	assert(px->key != NULL);
	assert(y != NULL);

	return strcmp(px->key, y);
}

/* ============================ optlist_exists () ========================== */
/* INPUT: optlist and str
 * OUTPUT: if optlist[str] exists 1 else 0
 */
gboolean optlist_exists(optlist_t * optlist, const char *str)
{
	assert(str != NULL);

	if(optlist == NULL)
		return 0;
	return g_list_find_custom(optlist, str, _compare) ? 1 : 0;
}

/* ============================ optlist_value () =========================== */
/* INPUT: optlist and str
 * OUTPUT: optlist[str] ("" if no value) else NULL
 */
const char *optlist_value(optlist_t * optlist, const char *str)
{
	GList *ptr;

	assert(str != NULL);

	if(optlist == NULL)
		return NULL;
	ptr = g_list_find_custom(optlist, str, _compare);

	assert(ptr != NULL || !optlist_exists(optlist, str));

	return (ptr != NULL) ? ((struct pair *)ptr->data)->val : NULL;
}

/* ============================ optlist_to_str () ========================== */
/* INPUT: str and optlist
 *        sizeof(str) >= MAX_PAR + 1
 * OUTPUT: string encapsulating optlist
 */
char *optlist_to_str(char *str, const optlist_t * optlist)
{
	const optlist_t *ptr = optlist;

	assert(str != NULL);

	*str = '\0';
	if(optlist != NULL)
		do {
                        struct pair *pair = ptr->data;
			strncat(str, pair->key, MAX_PAR - strlen(str));
			if(strlen(pair->val) > 0) {
				strncat(str, "=", MAX_PAR - strlen(str));
				strncat(str, pair->
					val, MAX_PAR - strlen(str));
			}
			if ((ptr = g_list_next(ptr)) != NULL)
				strncat(str, ",", MAX_PAR - strlen(str));
		} while (ptr);
	str[MAX_PAR] = '\0';

	assert((optlist == NULL && strlen(str) == 0) || strlen(str) > 0);

	return str;
}

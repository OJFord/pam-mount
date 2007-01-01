/*=============================================================================
xstdlib.h
  Copyright © Jan Engelhardt <jengelh [at] gmx de>, 2006 - 2007

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
#ifndef PMT_XSTDLIB_H
#define PMT_XSTDLIB_H 1

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int bool;

/*
 *      XSTDLIB.C
 */
extern void *xmalloc(size_t);
extern void *xmemdup(const void *, size_t);
extern void *xrealloc(void *, size_t);
extern char *xstrdup(const char *);
extern char *xstrndup(const char *, size_t);
extern void *xzalloc(size_t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PMT_XSTDLIB_H

//=============================================================================

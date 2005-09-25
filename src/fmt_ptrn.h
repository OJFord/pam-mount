#ifndef PMT_FMT_PTRN_H
#define PMT_FMT_PTRN_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <limits.h>
#include <glib.h>
#include <stdio.h>
#include <zlib.h>
#include "buffer.h"
#include "pair.h"

typedef struct fmt_ptrn_t {
    gzFile template_fp;
    char template_path[PATH_MAX + 1];
    long line_num;
    buffer_t raw_buf;           // Buffer for unfilled data
    buffer_t filled_buf;        // Buffer for filled data
    buffer_t lookup_buf;        // Lookup buffer; here so it is persistent
    char errmsg[BUFSIZ + 1];    // General errors
    GQueue *parse_errmsg;       // Parse errors
    GTree *fillers;             // Format pattern / value pairs
} fmt_ptrn_t;

extern int fmt_ptrn_close(fmt_ptrn_t *);
extern gboolean fmt_ptrn_parse_err(const fmt_ptrn_t *);
extern void enqueue_parse_errmsg(fmt_ptrn_t *, const char *, ...);
extern char *fmt_ptrn_filled(fmt_ptrn_t *, const char *);
extern char *fmt_ptrn_gets(char *, size_t, fmt_ptrn_t *);
extern int fmt_ptrn_init(fmt_ptrn_t *);
extern gboolean fmt_ptrn_open(const char *, fmt_ptrn_t *);
extern void fmt_ptrn_parse_perror(fmt_ptrn_t *, const char *);
extern char *fmt_ptrn_parse_strerror(fmt_ptrn_t *);
extern void fmt_ptrn_update_kv_p(fmt_ptrn_t *, const pair_t *);
extern void fmt_ptrn_update_kv(fmt_ptrn_t *, const char *, const char *);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PMT_FMT_PTRN_H

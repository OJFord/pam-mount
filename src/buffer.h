#ifndef PMT_BUFFER_H
#define PMT_BUFFER_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <glib.h>

typedef struct buffer_t {
    char *data;
    size_t size;
} buffer_t;

extern void buffer_clear(buffer_t *);
extern void buffer_destroy(buffer_t);
extern void buffer_eat(buffer_t, size_t);
extern buffer_t buffer_init(void);
extern size_t buffer_len(buffer_t *);
extern gboolean buffer_t_valid(const buffer_t *);
extern void realloc_n_cat(buffer_t *, const char *);
extern void realloc_n_cpy(buffer_t *, const char *);
extern void realloc_n_ncat(buffer_t *, const char *, size_t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PMT_BUFFER_H
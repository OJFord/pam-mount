#ifndef PMT_MOUNT_H
#define PMT_MONUT_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>
#include "fmt_ptrn.h"
#include "private.h"

extern int do_mount(config_t *, const unsigned int, fmt_ptrn_t *,
    const char *, const gboolean);
extern int do_unmount(config_t *, const unsigned int, fmt_ptrn_t *,
    const char * const, const gboolean);
extern int mount_op(int (*)(config_t *, const unsigned int, fmt_ptrn_t *,
    const char *, const int), config_t *, const unsigned int,
    const char *, const int);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PMT_MOUNT_H
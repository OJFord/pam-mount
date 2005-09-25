#ifndef PMT_DOTCONF_H
#define PMT_DOTCONF_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdio.h>

#define DOTCONF_CB(__name) const char *__name(command_t *cmd, context_t *ctx)
#define CTX_ALL 0 // context: option can be used anywhere
#define LAST_OPTION              {"", 0, NULL, NULL}
#define FUNC_ERRORHANDLER(_name) int _name(configfile_t *configfile, \
                                 int type, long dc_errno, const char *msg)

// constants for type of option
enum {
    ARG_TOGGLE = 0,
    ARG_INT,
    ARG_STR,
    ARG_LIST,
    ARG_NAME,
    ARG_RAW,
    ARG_NONE,
};

typedef void info_t;
typedef void context_t;
typedef struct configfile_t configfile_t;
typedef struct configoption_t configoption_t;
typedef struct command_t command_t;
typedef const char *(*dotconf_callback_t)(command_t *, context_t *);
typedef int (*dotconf_errorhandler_t)(configfile_t *, int, unsigned long, const char *);
typedef const char *(*dotconf_contextchecker_t)(command_t *, unsigned long);

struct command_t {
    const char *name;             		/* name of the command */
    configoption_t *option;		/* the option as given in the app; READ ONLY */

    // argument data filled in for each line / command
    struct {
        long value;     // ARG_INT, ARG_TOGGLE
        char *str;      // ARG_STR
        char **list;    // ARG_LIST
    } data;
    int arg_count;      // number of arguments (in data.list)

    // misc context information
    configfile_t *configfile;
    context_t *context;
};

struct configfile_t {
    /* ------ the fields in configfile_t are provided to the app
    via command_t's ; READ ONLY! --- */

    FILE *stream;
    char eof;           // end of file reached?
    size_t size;        // file size; cached on-demand for here-documents

    context_t *context;

    configoption_t const **config_options;
    int config_option_count;

    // misc read-only fields
    char *filename;             // name of file this option was found in
    unsigned long line;         // line number we're currently at
    unsigned long flags;        // runtime flags given to dotconf_open

    char *includepath;

    // some callbacks for interactivity
    dotconf_errorhandler_t errorhandler;
    dotconf_contextchecker_t contextchecker;
    int (*cmp_func)(const char *, const char *, size_t);
};

struct configoption_t {
    const char *name;								/* name of configuration option */
    int type;										/* for possible values, see above */
    dotconf_callback_t callback;        // callback function
    info_t *info;									/* additional info for multi-option callbacks */
    unsigned long context;              // context sensitivity flags
};

extern void dotconf_cleanup(configfile_t *);
extern int dotconf_command_loop(configfile_t *);
extern configfile_t *dotconf_create(const char *, const configoption_t *,
    context_t *, unsigned long);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PMT_DOTCONF_H
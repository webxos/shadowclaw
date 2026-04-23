#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* Command table – add your own commands here */
static Command commands[] = {
    { "boot",  boot_disk },
    { "diag",  run_diagnostic },
    { "status", get_status },
    { NULL, NULL }   /* sentinel */
};

/* ------------------------------------------------------------------
 * Dynamic string builder (internal use)
 * ------------------------------------------------------------------ */
typedef struct {
    char *str;
    size_t len;
    size_t cap;
} string_builder;

static void sb_init(string_builder *sb) {
    sb->str = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_append_char(string_builder *sb, char c) {
    if (sb->len + 2 > sb->cap) {   /* +1 for char, +1 for null */
        size_t new_cap = sb->cap ? sb->cap * 2 : 32;
        char *new_str = realloc(sb->str, new_cap);
        if (!new_str) return -1;
        sb->str = new_str;
        sb->cap = new_cap;
    }
    sb->str[sb->len++] = c;
    sb->str[sb->len] = '\0';
    return 0;
}

static int sb_append_str(string_builder *sb, const char *s) {
    while (*s) {
        if (sb_append_char(sb, *s++) < 0) return -1;
    }
    return 0;
}

static void sb_free(string_builder *sb) {
    free(sb->str);
    sb->str = NULL;
    sb->len = sb->cap = 0;
}

/* ------------------------------------------------------------------
 * Split quoted string into argv (handles escaped quotes and backslashes)
 * Returns NULL on error (memory or unterminated quote).
 * Caller must free each argv[i] and then free argv.
 * ------------------------------------------------------------------ */
static char** split_quoted(const char* input, int* argc) {
    char **argv = NULL;
    int capacity = 0;
    *argc = 0;
    const char *p = input;

    while (*p) {
        while (isspace(*p)) p++;
        if (!*p) break;

        if (*argc >= capacity) {
            capacity = capacity ? capacity * 2 : 8;
            char **new_argv = realloc(argv, capacity * sizeof(char*));
            if (!new_argv) goto error;
            argv = new_argv;
        }

        string_builder sb;
        sb_init(&sb);
        char quote = 0;

        if (*p == '"' || *p == '\'') {
            quote = *p++;
        }

        int escaped = 0;
        while (*p) {
            if (escaped) {
                if (sb_append_char(&sb, *p++) < 0) goto error;
                escaped = 0;
            } else if (*p == '\\') {
                escaped = 1;
                p++;
            } else if (quote && *p == quote) {
                p++; /* closing quote */
                break;
            } else if (!quote && isspace(*p)) {
                break;
            } else {
                if (sb_append_char(&sb, *p++) < 0) goto error;
            }
        }

        if (quote && !*p) {  /* unterminated quote */
            sb_free(&sb);
            goto error;
        }

        argv[(*argc)++] = sb.str;  /* sb.str is now owned by argv */
    }

    return argv;

error:
    for (int i = 0; i < *argc; i++) free(argv[i]);
    free(argv);
    return NULL;
}

/* ------------------------------------------------------------------
 * Main interpreter
 * ------------------------------------------------------------------ */
char* interpret_command(const char* input) {
    if (!input || *input == '\0')
        return strdup("No input provided.");

    int argc = 0;
    char **argv = split_quoted(input, &argc);
    if (!argv) return strdup("Memory error or unterminated quote.");

    if (argc == 0) {
        free(argv);
        return strdup("");
    }

    char *result = NULL;
    for (Command *c = commands; c->name; c++) {
        if (strcmp(c->name, argv[0]) == 0) {
            /* Build argument string safely */
            string_builder sb;
            sb_init(&sb);
            for (int i = 1; i < argc; i++) {
                if (i > 1) {
                    if (sb_append_char(&sb, ' ') < 0) {
                        sb_free(&sb);
                        for (int j = 0; j < argc; j++) free(argv[j]);
                        free(argv);
                        return strdup("Memory error");
                    }
                }
                if (sb_append_str(&sb, argv[i]) < 0) {
                    sb_free(&sb);
                    for (int j = 0; j < argc; j++) free(argv[j]);
                    free(argv);
                    return strdup("Memory error");
                }
            }
            result = c->fn(sb.str ? sb.str : "");
            sb_free(&sb);
            break;
        }
    }

    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);

    if (!result) result = strdup("Unknown command.");
    return result;
}

/* ------------------------------------------------------------------
 * Example command implementations
 * ------------------------------------------------------------------ */
char* boot_disk(const char* args) {
    /* Dynamic allocation to avoid truncation */
    size_t len = strlen("Booting disk with args: ") + strlen(args) + 1;
    char *buf = malloc(len);
    if (!buf) return strdup("Memory error");
    snprintf(buf, len, "Booting disk with args: %s", args);
    return buf;
}

char* run_diagnostic(const char* args) {
    (void)args;
    return strdup("Running diagnostic...");
}

char* get_status(const char* args) {
    (void)args;
    return strdup("Status: All systems nominal.");
}

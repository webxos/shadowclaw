#ifndef INTERPRETER_H
#define INTERPRETER_H

/* Command handler signature:
 *   Takes arguments string, returns malloc'd response.
 *   Caller is responsible for freeing the returned string.
 */
typedef char* (*CommandFn)(const char* args);

/* Registered command entry */
typedef struct {
    const char* name;
    CommandFn fn;
} Command;

/* Main interpreter entry point.
 *   Input: user command line (may contain quoted arguments).
 *   Returns: malloc'd result string (caller must free).
 */
char* interpret_command(const char* input);

/* Example command implementations – replace with real logic */
char* boot_disk(const char* args);
char* run_diagnostic(const char* args);
char* get_status(const char* args);

#endif /* INTERPRETER_H */

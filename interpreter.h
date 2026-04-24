#ifndef INTERPRETER_H
#define INTERPRETER_H

/* Special return value prefixes (for communication with main loop) */
#define INTERPRETER_MARKER_REPLAN     "REPLAN"
#define INTERPRETER_MARKER_TOOL_RESULT "TOOL_RESULT:"

/* Command handler signature:
 *   Takes arguments string, returns malloc'd response.
 *   Caller is responsible for freeing the returned string.
 *   If the returned string starts with one of the INTERPRETER_MARKER_*
 *   prefixes, the main loop will handle it specially.
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

/* Call this after executing any command that modifies persistent state
 * (e.g., adding/removing skills, cron jobs, webhooks, core memory).
 * The main loop will then sync the soul file.
 */
void interpreter_request_sync(void);

/* Example command implementations – replace with real logic */
char* boot_disk(const char* args);
char* run_diagnostic(const char* args);
char* get_status(const char* args);
char* cmd_replan(const char* args);   /* new: forces a fresh LLM call */

#endif /* INTERPRETER_H */

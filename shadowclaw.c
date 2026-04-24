#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>
#include "cJSON.h"
#include "interpreter.h"

// ------------------------------------------------------------------
// Version & Colors
// ------------------------------------------------------------------
#define SHADOWCLAW_VERSION "3.4"
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// ------------------------------------------------------------------
// Blob Storage (linked list, no fragmentation)
// ------------------------------------------------------------------
typedef enum {
    BLOB_SYSTEM = 1,
    BLOB_USER,
    BLOB_ASSISTANT,
    BLOB_TOOL_CALL,
    BLOB_TOOL_RESULT,
    BLOB_CORE_MEMORY,
    BLOB_SKILL,
    BLOB_CRONJOB,
    BLOB_WEBHOOK
} BlobKind;

typedef struct Blob {
    uint32_t kind;
    uint64_t id;
    size_t size;
    char *data;
    struct Blob *next;
} Blob;

static Blob *blob_list = NULL;
static uint64_t next_id = 1;
static int dirty = 0;

void blob_append(BlobKind kind, const char *data) {
    Blob *b = malloc(sizeof(Blob));
    if (!b) abort();
    b->kind = kind;
    b->id = next_id++;
    b->size = strlen(data);
    b->data = malloc(b->size + 1);
    if (!b->data) abort();
    strcpy(b->data, data);
    b->next = blob_list;
    blob_list = b;
    dirty = 1;
}

void blob_foreach_reverse(void (*cb)(Blob*, void*), void *user) {
    // reverse list to get chronological order
    Blob *prev = NULL, *curr = blob_list, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    curr = blob_list;
    while (curr) {
        cb(curr, user);
        curr = curr->next;
    }
    // restore original order
    prev = NULL;
    curr = blob_list;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
}

void blob_clear(void) {
    Blob *b = blob_list;
    while (b) {
        Blob *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    blob_list = NULL;
    next_id = 1;
    dirty = 1;
}

int save_state(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    uint32_t magic = 0x53484C42; // "SHLB"
    uint32_t version = 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&next_id, 8, 1, f);
    // Write blobs in order (oldest first)
    Blob *prev = NULL, *curr = blob_list, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    curr = blob_list;
    while (curr) {
        fwrite(&curr->kind, 4, 1, f);
        fwrite(&curr->id, 8, 1, f);
        fwrite(&curr->size, 8, 1, f);
        fwrite(curr->data, 1, curr->size + 1, f);
        curr = curr->next;
    }
    // restore
    prev = NULL;
    curr = blob_list;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    fclose(f);
    dirty = 0;
    return 0;
}

int load_state(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    uint32_t magic, version;
    if (fread(&magic, 4, 1, f) != 1 || magic != 0x53484C42) {
        fclose(f);
        return -1;
    }
    if (fread(&version, 4, 1, f) != 1 || version != 1) {
        fclose(f);
        return -1;
    }
    if (fread(&next_id, 8, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    blob_clear();
    while (!feof(f)) {
        uint32_t kind;
        uint64_t id;
        size_t size;
        if (fread(&kind, 4, 1, f) != 1) break;
        if (fread(&id, 8, 1, f) != 1) break;
        if (fread(&size, 8, 1, f) != 1) break;
        char *data = malloc(size + 1);
        if (!data) { fclose(f); return -1; }
        if (fread(data, 1, size + 1, f) != size + 1) {
            free(data);
            fclose(f);
            return -1;
        }
        Blob *b = malloc(sizeof(Blob));
        if (!b) { free(data); fclose(f); return -1; }
        b->kind = kind;
        b->id = id;
        b->size = size;
        b->data = data;
        b->next = blob_list;
        blob_list = b;
    }
    fclose(f);
    dirty = 0;
    return 0;
}

void sync_soul_file(const char *soul_file) {
    FILE *f = fopen(soul_file, "w");
    if (!f) return;
    fprintf(f, "# ShadowSoul - Unified Memory\n");
    time_t now = time(NULL);
    fprintf(f, "Last updated: %s", ctime(&now));
    fprintf(f, "\n## Core Memory\n");
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CORE_MEMORY)
            fprintf(f, "```json\n%s\n```\n\n", b->data);
        b = b->next;
    }
    fprintf(f, "## Skills\n");
    b = blob_list;
    while (b) {
        if (b->kind == BLOB_SKILL)
            fprintf(f, "- %s\n", b->data);
        b = b->next;
    }
    fprintf(f, "\n## Cron Jobs\n");
    b = blob_list;
    while (b) {
        if (b->kind == BLOB_CRONJOB)
            fprintf(f, "- %s\n", b->data);
        b = b->next;
    }
    fprintf(f, "\n## Webhooks\n");
    b = blob_list;
    while (b) {
        if (b->kind == BLOB_WEBHOOK)
            fprintf(f, "- %s\n", b->data);
        b = b->next;
    }
    fprintf(f, "\n## Conversation Log\n");
    // Print in chronological order
    Blob *prev = NULL, *curr = blob_list, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    curr = blob_list;
    while (curr) {
        if (curr->kind == BLOB_USER || curr->kind == BLOB_ASSISTANT ||
            curr->kind == BLOB_TOOL_CALL || curr->kind == BLOB_TOOL_RESULT) {
            const char *role = "";
            switch (curr->kind) {
                case BLOB_USER: role = "User"; break;
                case BLOB_ASSISTANT: role = "Assistant"; break;
                case BLOB_TOOL_CALL: role = "Tool Call"; break;
                case BLOB_TOOL_RESULT: role = "Tool Result"; break;
                default: break;
            }
            fprintf(f, "### %s\n%s\n\n", role, curr->data);
        }
        curr = curr->next;
    }
    // restore
    prev = NULL;
    curr = blob_list;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    fclose(f);
}

// ------------------------------------------------------------------
// Tools (with runtime filtering)
// ------------------------------------------------------------------
typedef struct Tool {
    const char *name;
    char *(*func)(const char *args);
    const char *description;
    int enabled;
} Tool;

// Forward declarations
char* tool_shell(const char *args);
char* tool_file_read(const char *args);
char* tool_file_write(const char *args);
char* tool_http_get(const char *args);
char* tool_math(const char *args);
char* tool_list_dir(const char *args);
char* tool_webhook_add(const char *args);
char* tool_cron_add(const char *args);
char* tool_cron_list(const char *args);
char* tool_cron_remove(const char *args);
char* tool_skill_add(const char *args);
char* tool_skill_run(const char *args);
char* tool_list_skills(const char *args);
char* tool_update_core_memory(const char *args);
char* tool_recall(const char *args);
char* tool_heartbeat(const char *args);

// Implementations (simplified but functional)
char* tool_shell(const char *args) {
    (void)args;
    return strdup("Shell tool is disabled for security.");
}

#define MAX_FILE_SIZE (10 * 1024 * 1024)
#define MAX_TOOL_RESULT 2000

char* tool_file_read(const char *args) {
    FILE *f = fopen(args, "rb");
    if (!f) return strdup("Error: cannot open file");
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return strdup("Error seeking file"); }
    long size = ftell(f);
    if (size == -1 || size > MAX_FILE_SIZE) { fclose(f); return strdup("File too large"); }
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return strdup("Memory error"); }
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    if (strlen(buf) > MAX_TOOL_RESULT) buf[MAX_TOOL_RESULT] = '\0';
    return buf;
}

char* tool_file_write(const char *args) {
    const char *space = strchr(args, ' ');
    if (!space) return strdup("Invalid: need 'filename content'");
    size_t fname_len = space - args;
    char *fname = malloc(fname_len + 1);
    if (!fname) return strdup("Memory error");
    memcpy(fname, args, fname_len);
    fname[fname_len] = '\0';
    const char *content = space + 1;
    FILE *f = fopen(fname, "w");
    free(fname);
    if (!f) return strdup("Cannot write file");
    fprintf(f, "%s", content);
    fclose(f);
    return strdup("File written");
}

static size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    size_t total = size * nmemb;
    char **resp = (char**)user;
    size_t old_len = *resp ? strlen(*resp) : 0;
    char *new = realloc(*resp, old_len + total + 1);
    if (!new) return 0;
    *resp = new;
    memcpy(*resp + old_len, ptr, total);
    (*resp)[old_len + total] = '\0';
    return total;
}

char* tool_http_get(const char *args) {
    CURL *curl = curl_easy_init();
    if (!curl) return strdup("curl init failed");
    char *response = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, args);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        free(response);
        return strdup("HTTP request failed");
    }
    if (response && strlen(response) > MAX_TOOL_RESULT) response[MAX_TOOL_RESULT] = '\0';
    return response ? response : strdup("");
}

char* tool_math(const char *args) {
    // simple eval using bc
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo '%s' | bc 2>/dev/null", args);
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("Math error");
    char buf[256];
    char *result = NULL;
    size_t len = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        size_t l = strlen(buf);
        char *new = realloc(result, len + l + 1);
        if (!new) { free(result); pclose(fp); return strdup("Memory error"); }
        result = new;
        memcpy(result + len, buf, l + 1);
        len += l;
    }
    pclose(fp);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT) result[MAX_TOOL_RESULT] = '\0';
    return result;
}

char* tool_list_dir(const char *args) {
    DIR *d = opendir(args);
    if (!d) return strdup("Cannot open directory");
    char *result = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&result, &len);
    if (!out) { closedir(d); return strdup("Memory error"); }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) fprintf(out, "%s\n", entry->d_name);
    closedir(d);
    fclose(out);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT) result[MAX_TOOL_RESULT] = '\0';
    return result;
}

char* tool_webhook_add(const char *args) {
    cJSON *j = cJSON_Parse(args);
    if (!j) return strdup("Invalid JSON");
    cJSON *url = cJSON_GetObjectItem(j, "url");
    cJSON *event = cJSON_GetObjectItem(j, "event");
    if (!cJSON_IsString(url) || !cJSON_IsString(event)) {
        cJSON_Delete(j);
        return strdup("Missing url or event");
    }
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "url", url->valuestring);
    cJSON_AddStringToObject(obj, "event", event->valuestring);
    char *entry = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    cJSON_Delete(j);
    if (entry) {
        blob_append(BLOB_WEBHOOK, entry);
        free(entry);
        return strdup("Webhook added");
    }
    return strdup("Memory error");
}

char* tool_cron_add(const char *args) {
    cJSON *j = cJSON_Parse(args);
    if (!j) return strdup("Invalid JSON");
    cJSON *sched = cJSON_GetObjectItem(j, "schedule");
    cJSON *tool = cJSON_GetObjectItem(j, "tool");
    cJSON *tool_args = cJSON_GetObjectItem(j, "args");
    if (!cJSON_IsString(sched) || !cJSON_IsString(tool) || !cJSON_IsString(tool_args)) {
        cJSON_Delete(j);
        return strdup("Missing schedule, tool, or args");
    }
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "schedule", sched->valuestring);
    cJSON_AddStringToObject(obj, "tool", tool->valuestring);
    cJSON_AddStringToObject(obj, "args", tool_args->valuestring);
    cJSON_AddNumberToObject(obj, "last_run", 0);
    char *entry = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    cJSON_Delete(j);
    if (entry) {
        blob_append(BLOB_CRONJOB, entry);
        free(entry);
        return strdup("Cron job added");
    }
    return strdup("Memory error");
}

char* tool_cron_list(const char *args) {
    (void)args;
    char *result = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&result, &len);
    if (!out) return strdup("Memory error");
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CRONJOB) fprintf(out, "%s\n", b->data);
        b = b->next;
    }
    fclose(out);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT) result[MAX_TOOL_RESULT] = '\0';
    return result;
}

char* tool_cron_remove(const char *args) {
    Blob *prev = NULL, *curr = blob_list;
    int removed = 0;
    while (curr) {
        if (curr->kind == BLOB_CRONJOB && strstr(curr->data, args)) {
            Blob *next = curr->next;
            if (prev) prev->next = next;
            else blob_list = next;
            free(curr->data);
            free(curr);
            curr = next;
            removed++;
            dirty = 1;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Removed %d cron job(s)", removed);
    return strdup(msg);
}

char* tool_skill_add(const char *args) {
    cJSON *j = cJSON_Parse(args);
    if (!j) return strdup("Invalid JSON");
    cJSON *name = cJSON_GetObjectItem(j, "name");
    cJSON *desc = cJSON_GetObjectItem(j, "desc");
    cJSON *steps = cJSON_GetObjectItem(j, "steps");
    if (!cJSON_IsString(name) || !cJSON_IsString(desc) || !cJSON_IsArray(steps)) {
        cJSON_Delete(j);
        return strdup("Missing name, desc, or steps array");
    }
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", name->valuestring);
    cJSON_AddStringToObject(obj, "desc", desc->valuestring);
    cJSON_AddItemToObject(obj, "steps", cJSON_Duplicate(steps, 1));
    char *entry = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    cJSON_Delete(j);
    if (entry) {
        blob_append(BLOB_SKILL, entry);
        free(entry);
        return strdup("Skill added");
    }
    return strdup("Memory error");
}

char* tool_skill_run(const char *args) {
    char *copy = strdup(args);
    if (!copy) return strdup("Memory error");
    char *space = strchr(copy, ' ');
    if (space) *space = '\0';
    const char *skill_name = copy;
    const char *skill_args = space ? space + 1 : "";
    char *result = NULL;
    asprintf(&result, "RUN_SKILL:%s:%s", skill_name, skill_args);
    free(copy);
    return result ? result : strdup("");
}

char* tool_list_skills(const char *args) {
    (void)args;
    char *result = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&result, &len);
    if (!out) return strdup("Memory error");
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_SKILL) {
            cJSON *j = cJSON_Parse(b->data);
            if (j) {
                cJSON *name = cJSON_GetObjectItem(j, "name");
                if (cJSON_IsString(name)) fprintf(out, "%s\n", name->valuestring);
                cJSON_Delete(j);
            }
        }
        b = b->next;
    }
    fclose(out);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT) result[MAX_TOOL_RESULT] = '\0';
    return result;
}

char* tool_update_core_memory(const char *args) {
    cJSON *updates = cJSON_Parse(args);
    if (!updates) return strdup("Invalid JSON");
    // Find existing core memory
    cJSON *core = NULL;
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CORE_MEMORY) {
            core = cJSON_Parse(b->data);
            break;
        }
        b = b->next;
    }
    if (!core) core = cJSON_CreateObject();
    // Merge updates
    cJSON *child = NULL;
    cJSON_ArrayForEach(child, updates) {
        cJSON_AddItemToObject(core, child->string, cJSON_Duplicate(child, 1));
    }
    char *new_core = cJSON_PrintUnformatted(core);
    cJSON_Delete(core);
    cJSON_Delete(updates);
    if (!new_core) return strdup("Memory error");
    // Delete old core blob
    Blob *prev = NULL, *curr = blob_list;
    while (curr) {
        if (curr->kind == BLOB_CORE_MEMORY) {
            if (prev) prev->next = curr->next;
            else blob_list = curr->next;
            free(curr->data);
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    blob_append(BLOB_CORE_MEMORY, new_core);
    free(new_core);
    dirty = 1;
    return strdup("Core memory updated");
}

char* tool_recall(const char *args) {
    char *result = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&result, &len);
    if (!out) return strdup("Memory error");
    Blob *b = blob_list;
    while (b) {
        if ((b->kind == BLOB_USER || b->kind == BLOB_ASSISTANT) && strcasestr(b->data, args)) {
            const char *role = (b->kind == BLOB_USER) ? "User" : "Assistant";
            fprintf(out, "[%s] %s\n", role, b->data);
        }
        b = b->next;
    }
    fclose(out);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT) result[MAX_TOOL_RESULT] = '\0';
    return result;
}

char* tool_heartbeat(const char *args) {
    (void)args;
    return strdup("Heartbeat");
}

static Tool tools[] = {
    {"shell", tool_shell, "Execute shell command (disabled)", 0},
    {"file_read", tool_file_read, "Read a file", 1},
    {"file_write", tool_file_write, "Write a file", 1},
    {"http_get", tool_http_get, "HTTP GET request", 1},
    {"math", tool_math, "Evaluate arithmetic expression", 1},
    {"list_dir", tool_list_dir, "List directory contents", 1},
    {"webhook_add", tool_webhook_add, "Add a webhook", 1},
    {"cron_add", tool_cron_add, "Add a cron job", 1},
    {"cron_list", tool_cron_list, "List cron jobs", 1},
    {"cron_remove", tool_cron_remove, "Remove cron jobs", 1},
    {"skill_add", tool_skill_add, "Add a skill", 1},
    {"skill_run", tool_skill_run, "Run a skill", 1},
    {"list_skills", tool_list_skills, "List skills", 1},
    {"update_core_memory", tool_update_core_memory, "Update core memory", 1},
    {"recall", tool_recall, "Search conversation history", 1},
    {"heartbeat", tool_heartbeat, "Internal heartbeat", 1},
    {NULL, NULL, NULL, 0}
};

char* execute_tool(const char *name, const char *args) {
    for (Tool *t = tools; t->name; t++) {
        if (strcmp(t->name, name) == 0 && t->enabled) {
            return t->func(args);
        }
    }
    return strdup("Unknown or disabled tool");
}

// ------------------------------------------------------------------
// LLM Communication
// ------------------------------------------------------------------
static int no_llm_mode = 0;
static int dry_run = 0;
static char state_filename[256] = "shadowclaw.bin";
static char soul_filename[256] = "shadowclaw_data/shadowsoul.md";
static const char *ollama_endpoint = "http://localhost:11434";
static const char *ollama_model = "qwen2.5:0.5b";
static long llm_connect_timeout = 15;
static long llm_total_timeout = 180;
static int llm_retry_attempts = 5;
static int shell_enabled = 0;

static int ollama_healthy(void) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_URL, ollama_endpoint);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

typedef struct {
    char *data;
    size_t len;
} RespBuf;

static size_t llm_write_cb(void *ptr, size_t size, size_t nmemb, void *stream) {
    RespBuf *buf = (RespBuf*)stream;
    size_t total = size * nmemb;
    char *new = realloc(buf->data, buf->len + total + 1);
    if (!new) return 0;
    buf->data = new;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static char* call_llm(const char *prompt) {
    if (!ollama_healthy()) {
        fprintf(stderr, COLOR_YELLOW "Ollama unreachable. Switching to no-llm mode.\n" COLOR_RESET);
        no_llm_mode = 1;
        return NULL;
    }
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/generate", ollama_endpoint);
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", ollama_model);
    cJSON_AddStringToObject(req, "prompt", prompt);
    cJSON_AddBoolToObject(req, "stream", 0);
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    RespBuf resp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, llm_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, llm_connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, llm_total_timeout);
    CURLcode res = curl_easy_perform(curl);
    free(req_str);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, COLOR_RED "LLM request failed: %s\n" COLOR_RESET, curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }
    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!root) return NULL;
    cJSON *response = cJSON_GetObjectItem(root, "response");
    char *result = response && cJSON_IsString(response) ? strdup(response->valuestring) : NULL;
    cJSON_Delete(root);
    return result;
}

static char* build_simple_prompt(const char *user_msg) {
    // Build list of available tools (filter by enabled)
    char tools_list[1024] = "";
    for (Tool *t = tools; t->name; t++) {
        if (t->enabled) {
            strcat(tools_list, t->name);
            strcat(tools_list, ", ");
        }
    }
    if (strlen(tools_list) > 2) tools_list[strlen(tools_list)-2] = '\0';
    // Collect core memory
    char *core = strdup("{}");
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CORE_MEMORY) {
            free(core);
            core = strdup(b->data);
            break;
        }
        b = b->next;
    }
    // Collect last 5 conversation turns (chronological)
    char history[4096] = "";
    Blob *prev = NULL, *curr = blob_list, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    int count = 0;
    curr = blob_list;
    while (curr && count < 10) { // 5 pairs
        if (curr->kind == BLOB_USER || curr->kind == BLOB_ASSISTANT ||
            curr->kind == BLOB_TOOL_CALL || curr->kind == BLOB_TOOL_RESULT) {
            const char *role = "";
            switch (curr->kind) {
                case BLOB_USER: role = "User"; break;
                case BLOB_ASSISTANT: role = "Assistant"; break;
                case BLOB_TOOL_CALL: role = "Tool call"; break;
                case BLOB_TOOL_RESULT: role = "Tool result"; break;
            }
            char line[512];
            snprintf(line, sizeof(line), "%s: %s\n", role, curr->data);
            strcat(history, line);
            count++;
        }
        curr = curr->next;
    }
    // restore list
    prev = NULL;
    curr = blob_list;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    blob_list = prev;
    char *prompt;
    asprintf(&prompt,
        "You are Shadowclaw, an AI assistant with access to tools.\n"
        "Core memory: %s\n\n"
        "Recent conversation:\n%s\n\n"
        "Available tools: %s\n"
        "To use a tool, output exactly:\n```tool\n{\"tool\":\"name\",\"args\":\"arguments\"}\n```\n"
        "Otherwise, respond normally.\n\n"
        "User: %s\n",
        core, history, tools_list, user_msg);
    free(core);
    return prompt;
}

static char* extract_tool_json(const char *response) {
    const char *start = strstr(response, "```tool");
    if (!start) return NULL;
    start += 7;
    while (*start == ' ' || *start == '\n') start++;
    const char *end = strstr(start, "```");
    if (!end) return NULL;
    size_t len = end - start;
    char *json = malloc(len + 1);
    if (!json) return NULL;
    memcpy(json, start, len);
    json[len] = '\0';
    return json;
}

static int parse_tool_call(const char *json_str, char **tool_name, char **tool_args) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return 0;
    cJSON *name = cJSON_GetObjectItem(root, "tool");
    cJSON *args = cJSON_GetObjectItem(root, "args");
    if (!cJSON_IsString(name)) {
        cJSON_Delete(root);
        return 0;
    }
    *tool_name = strdup(name->valuestring);
    if (cJSON_IsString(args)) {
        *tool_args = strdup(args->valuestring);
    } else if (cJSON_IsArray(args)) {
        int size = cJSON_GetArraySize(args);
        size_t total = 0;
        for (int i = 0; i < size; i++) {
            cJSON *elem = cJSON_GetArrayItem(args, i);
            if (cJSON_IsString(elem)) total += strlen(elem->valuestring) + 1;
        }
        *tool_args = malloc(total + 1);
        if (*tool_args) {
            (*tool_args)[0] = '\0';
            for (int i = 0; i < size; i++) {
                cJSON *elem = cJSON_GetArrayItem(args, i);
                if (cJSON_IsString(elem)) {
                    if (i > 0) strcat(*tool_args, " ");
                    strcat(*tool_args, elem->valuestring);
                }
            }
        } else {
            *tool_args = strdup("");
        }
    } else {
        *tool_args = strdup("");
    }
    cJSON_Delete(root);
    return 1;
}

// Webhook retry
static int http_post_with_retry(const char *url, const char *payload) {
    int delays[] = {1, 2, 4};
    for (int i = 0; i < 3; i++) {
        CURL *curl = curl_easy_init();
        if (!curl) continue;
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite); // discard
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        if (res == CURLE_OK) return 0;
        sleep(delays[i]);
    }
    fprintf(stderr, COLOR_RED "Webhook POST failed after retries\n" COLOR_RESET);
    return -1;
}

static void trigger_webhooks(const char *event, const char *data) {
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_WEBHOOK) {
            cJSON *j = cJSON_Parse(b->data);
            if (j) {
                cJSON *url = cJSON_GetObjectItem(j, "url");
                cJSON *ev = cJSON_GetObjectItem(j, "event");
                if (cJSON_IsString(url) && cJSON_IsString(ev) && strcmp(ev->valuestring, event) == 0) {
                    cJSON *payload = cJSON_CreateObject();
                    cJSON_AddStringToObject(payload, "event", event);
                    cJSON_AddStringToObject(payload, "data", data);
                    char *payload_str = cJSON_PrintUnformatted(payload);
                    cJSON_Delete(payload);
                    if (payload_str) {
                        http_post_with_retry(url->valuestring, payload_str);
                        free(payload_str);
                    }
                }
                cJSON_Delete(j);
            }
        }
        b = b->next;
    }
}

// Cron processing in main loop
static void process_cron_jobs(void) {
    time_t now = time(NULL);
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CRONJOB) {
            cJSON *j = cJSON_Parse(b->data);
            if (!j) { b = b->next; continue; }
            cJSON *sched = cJSON_GetObjectItem(j, "schedule");
            cJSON *tool = cJSON_GetObjectItem(j, "tool");
            cJSON *args = cJSON_GetObjectItem(j, "args");
            cJSON *last_run_json = cJSON_GetObjectItem(j, "last_run");
            if (cJSON_IsString(sched) && cJSON_IsString(tool) && cJSON_IsString(args) && cJSON_IsNumber(last_run_json)) {
                time_t last_run = (time_t)last_run_json->valuedouble;
                int interval = 0;
                if (strcmp(sched->valuestring, "@heartbeat") == 0) interval = 120;
                else if (strncmp(sched->valuestring, "@every ", 7) == 0) {
                    int val = atoi(sched->valuestring + 7);
                    interval = val;
                } else if (strcmp(sched->valuestring, "@hourly") == 0) interval = 3600;
                else if (strcmp(sched->valuestring, "@daily") == 0) interval = 86400;
                else if (strcmp(sched->valuestring, "@weekly") == 0) interval = 604800;
                if (interval > 0 && (now - last_run) >= interval) {
                    if (strcmp(tool->valuestring, "heartbeat") == 0) {
                        // run heartbeat immediately
                        char *result = execute_tool("heartbeat", "");
                        printf(COLOR_CYAN "[Cron] Heartbeat: %s\n" COLOR_RESET, result);
                        free(result);
                    } else {
                        // queue? For simplicity, just execute now
                        char *result = execute_tool(tool->valuestring, args->valuestring);
                        printf(COLOR_CYAN "[Cron] %s(%s) -> %s\n" COLOR_RESET, tool->valuestring, args->valuestring, result);
                        free(result);
                    }
                    // update last_run in blob
                    cJSON_SetNumberValue(last_run_json, (double)now);
                    char *new_data = cJSON_PrintUnformatted(j);
                    if (new_data) {
                        // replace blob
                        Blob *prev = NULL, *curr = blob_list;
                        while (curr) {
                            if (curr == b) {
                                if (prev) prev->next = curr->next;
                                else blob_list = curr->next;
                                free(curr->data);
                                free(curr);
                                break;
                            }
                            prev = curr;
                            curr = curr->next;
                        }
                        blob_append(BLOB_CRONJOB, new_data);
                        free(new_data);
                        dirty = 1;
                        break; // restart iteration
                    }
                }
            }
            cJSON_Delete(j);
        }
        b = b->next;
    }
}

// ------------------------------------------------------------------
// Slash Commands (restored full menu)
// ------------------------------------------------------------------
static void print_help(void) {
    printf(COLOR_BOLD "\nShadowclaw v%s commands:\n" COLOR_RESET, SHADOWCLAW_VERSION);
    printf("  %-12s %s\n", "/help", "Show this help");
    printf("  %-12s %s\n", "/tools", "List available tools");
    printf("  %-12s %s\n", "/state", "Show arena memory stats and soul file info");
    printf("  %-12s %s\n", "/clear", "Clear conversation history");
    printf("  %-12s %s\n", "/exit", "Exit Shadowclaw");
    printf("  %-12s %s\n", "/loop", "Schedule a recurring task: /loop <schedule> <tool> [args...]");
    printf("  %-12s %s\n", "/crons", "List scheduled cron jobs");
    printf("  %-12s %s\n", "/webhooks", "List registered webhooks");
    printf("  %-12s %s\n", "/skills", "List dynamic skills");
    printf("  %-12s %s\n", "/compact", "Manually compact arena (remove deleted blobs)");
    printf("  %-12s %s\n", "/soul", "Show path to shadowsoul.md and last update");
    printf("  %-12s %s\n", "/reconnect", "Reconnect to Ollama (if LLM mode is off)");

    printf(COLOR_BOLD "\nBuilt-in tools:\n" COLOR_RESET);
    for (Tool *t = tools; t->name; t++) printf("  %s\n", t->name);

    int skill_count = 0;
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_SKILL) {
            cJSON *j = cJSON_Parse(b->data);
            if (j) {
                cJSON *name = cJSON_GetObjectItem(j, "name");
                cJSON *desc = cJSON_GetObjectItem(j, "desc");
                if (cJSON_IsString(name) && cJSON_IsString(desc)) {
                    if (skill_count == 0) printf(COLOR_BOLD "\nDynamic skills:\n" COLOR_RESET);
                    printf("  %-12s %s\n", name->valuestring, desc->valuestring);
                    skill_count++;
                }
                cJSON_Delete(j);
            }
        }
        b = b->next;
    }
    if (skill_count == 0) printf("\nNo dynamic skills loaded. Use skill_add to create one.\n");

    struct stat st;
    if (stat(soul_filename, &st) == 0) {
        char timebuf[128];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%c", tm_info);
        printf("\nSoul file: %s (%ld bytes, last modified: %s\n", soul_filename, st.st_size, timebuf);
    } else {
        printf("\nSoul file not yet created.\n");
    }
    printf("\n");
}

static void list_tools(void) {
    printf(COLOR_BOLD "Enabled tools:\n" COLOR_RESET);
    for (Tool *t = tools; t->name; t++) {
        if (t->enabled)
            printf("  %-12s %s\n", t->name, t->description);
    }
}

static void show_state(void) {
    size_t total_size = 0;
    Blob *b = blob_list;
    while (b) {
        total_size += sizeof(Blob) + b->size + 1;
        b = b->next;
    }
    printf("Blob count: %llu (next id: %llu)\n", (unsigned long long)(next_id - 1), (unsigned long long)next_id);
    printf("Estimated memory usage: ~%zu bytes\n", total_size);
    struct stat st;
    if (stat(soul_filename, &st) == 0) {
        char timebuf[128];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%c", tm_info);
        printf("Soul file: %s (%ld bytes, last modified: %s\n", soul_filename, st.st_size, timebuf);
    } else {
        printf("Soul file not yet created.\n");
    }
}

static void clear_conversation(void) {
    // Keep system, core memory, skills, cron jobs, webhooks
    Blob *new_list = NULL;
    Blob *b = blob_list;
    while (b) {
        Blob *next = b->next;
        if (b->kind == BLOB_SYSTEM || b->kind == BLOB_CORE_MEMORY ||
            b->kind == BLOB_SKILL || b->kind == BLOB_CRONJOB || b->kind == BLOB_WEBHOOK) {
            // move to new list (preserve order)
            b->next = new_list;
            new_list = b;
        } else {
            free(b->data);
            free(b);
        }
        b = next;
    }
    blob_list = new_list;
    dirty = 1;
    printf("Conversation history cleared (system, core memory, skills, cron, webhooks preserved).\n");
}

static void compact_arena(void) {
    // Rebuild blob list without any deleted markers (we don't use deleted markers, so just reorder?)
    // Actually we want to reorder by id to remove gaps and keep chronological? 
    // For simplicity, we just leave as is; compaction is automatic via save/load.
    // Calling save and reload compacts.
    if (save_state(state_filename) == 0) {
        load_state(state_filename);
        printf("Arena compacted (state saved and reloaded).\n");
    } else {
        printf("Compaction failed.\n");
    }
}

static const char* parse_schedule_string(const char *sched) {
    static char buf[32];
    if (strcmp(sched, "hourly") == 0) return "@hourly";
    if (strcmp(sched, "daily") == 0) return "@daily";
    if (strcmp(sched, "weekly") == 0) return "@weekly";
    char *end;
    long val = strtol(sched, &end, 10);
    if (end != sched && *end) {
        if (strcmp(end, "s") == 0) { snprintf(buf, sizeof(buf), "@every %lds", val); return buf; }
        if (strcmp(end, "m") == 0) { snprintf(buf, sizeof(buf), "@every %ldm", val); return buf; }
        if (strcmp(end, "h") == 0) { snprintf(buf, sizeof(buf), "@every %ldh", val); return buf; }
    }
    return NULL;
}

// ------------------------------------------------------------------
// Main
// ------------------------------------------------------------------
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-llm") == 0) no_llm_mode = 1;
        else if (strcmp(argv[i], "--dry-run") == 0) dry_run = 1;
        else if (strcmp(argv[i], "--enable-shell") == 0) {
            shell_enabled = 1;
            for (Tool *t = tools; t->name; t++)
                if (strcmp(t->name, "shell") == 0) t->enabled = 1;
        } else if (strcmp(argv[i], "--model") == 0 && i+1 < argc) ollama_model = argv[++i];
        else if (strcmp(argv[i], "-f") == 0 && i+1 < argc) strncpy(state_filename, argv[++i], 255);
    }

    curl_global_init(CURL_GLOBAL_ALL);
    
    // Ensure soul directory exists
    char *soul_dir = strdup(soul_filename);
    char *last_slash = strrchr(soul_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(soul_dir, 0755);
    }
    free(soul_dir);
    
    if (load_state(state_filename) != 0) {
        printf("No saved state found. Starting fresh.\n");
        blob_append(BLOB_SYSTEM, "You are Shadowclaw, a helpful AI assistant with access to tools.");
        blob_append(BLOB_CORE_MEMORY, "{}");
        save_state(state_filename);
    }
    sync_soul_file(soul_filename);

    printf(COLOR_BOLD "\nShadowclaw v%s – Ollama-Enhanced\n" COLOR_RESET, SHADOWCLAW_VERSION);
    if (no_llm_mode) printf(COLOR_YELLOW "LLM mode disabled. Using local interpreter.\n" COLOR_RESET);
    else printf(COLOR_GREEN "LLM mode enabled (model: %s)\n" COLOR_RESET, ollama_model);
    
    // Ensure heartbeat cron job exists
    int has_heartbeat = 0;
    Blob *b = blob_list;
    while (b) {
        if (b->kind == BLOB_CRONJOB && strstr(b->data, "\"tool\":\"heartbeat\"")) {
            has_heartbeat = 1;
            break;
        }
        b = b->next;
    }
    if (!has_heartbeat) {
        char *heartbeat_json = NULL;
        asprintf(&heartbeat_json, "{\"schedule\":\"@every 120s\",\"tool\":\"heartbeat\",\"args\":\"\",\"last_run\":0}");
        if (heartbeat_json) {
            blob_append(BLOB_CRONJOB, heartbeat_json);
            free(heartbeat_json);
            dirty = 1;
        }
    }

    char line[4096];
    while (1) {
        process_cron_jobs();
        printf(COLOR_GREEN "> " COLOR_RESET);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

        if (line[0] == '/') {
            if (strcmp(line, "/help") == 0) {
                print_help();
            } else if (strcmp(line, "/tools") == 0) {
                list_tools();
            } else if (strcmp(line, "/state") == 0) {
                show_state();
            } else if (strcmp(line, "/clear") == 0) {
                clear_conversation();
                save_state(state_filename);
                sync_soul_file(soul_filename);
            } else if (strcmp(line, "/exit") == 0) {
                break;
            } else if (strncmp(line, "/loop", 5) == 0) {
                char *p = line + 5;
                while (*p == ' ') p++;
                if (*p == '\0') {
                    printf("Usage: /loop <schedule> <tool> [args...]\n"
                           "Examples:\n  /loop 1h http_get https://example.com/news\n"
                           "  /loop daily file_read ~/notes.txt\n");
                    continue;
                }
                char *sched_start = p;
                while (*p && !isspace(*p)) p++;
                if (!*p) { printf("Missing tool after schedule.\n"); continue; }
                *p++ = '\0';
                const char *sched_str = parse_schedule_string(sched_start);
                if (!sched_str) {
                    printf("Unrecognized schedule format. Use e.g., 1h, 30m, daily, hourly, weekly.\n");
                    continue;
                }
                while (*p == ' ') p++;
                if (*p == '\0') { printf("Missing tool name.\n"); continue; }
                char *tool_start = p;
                while (*p && !isspace(*p)) p++;
                if (*p) *p++ = '\0';
                while (*p == ' ') p++;
                char *args = p;
                
                cJSON *obj = cJSON_CreateObject();
                cJSON_AddStringToObject(obj, "schedule", sched_str);
                cJSON_AddStringToObject(obj, "tool", tool_start);
                cJSON_AddStringToObject(obj, "args", args);
                cJSON_AddNumberToObject(obj, "last_run", 0);
                char *json = cJSON_PrintUnformatted(obj);
                cJSON_Delete(obj);
                if (json) {
                    blob_append(BLOB_CRONJOB, json);
                    free(json);
                    save_state(state_filename);
                    sync_soul_file(soul_filename);
                    printf("Scheduled: %s every %s\n", tool_start, sched_str);
                } else {
                    printf("Failed to create cron job.\n");
                }
            } else if (strcmp(line, "/crons") == 0) {
                Blob *b = blob_list;
                printf("Scheduled cron jobs:\n");
                int count = 0;
                while (b) {
                    if (b->kind == BLOB_CRONJOB) {
                        printf("  %s\n", b->data);
                        count++;
                    }
                    b = b->next;
                }
                if (count == 0) printf("  None\n");
            } else if (strcmp(line, "/webhooks") == 0) {
                Blob *b = blob_list;
                printf("Registered webhooks:\n");
                int count = 0;
                while (b) {
                    if (b->kind == BLOB_WEBHOOK) {
                        printf("  %s\n", b->data);
                        count++;
                    }
                    b = b->next;
                }
                if (count == 0) printf("  None\n");
            } else if (strcmp(line, "/skills") == 0) {
                Blob *b = blob_list;
                printf("Dynamic skills:\n");
                int count = 0;
                while (b) {
                    if (b->kind == BLOB_SKILL) {
                        cJSON *j = cJSON_Parse(b->data);
                        if (j) {
                            cJSON *name = cJSON_GetObjectItem(j, "name");
                            cJSON *desc = cJSON_GetObjectItem(j, "desc");
                            if (cJSON_IsString(name) && cJSON_IsString(desc)) {
                                printf("  %s: %s\n", name->valuestring, desc->valuestring);
                                count++;
                            }
                            cJSON_Delete(j);
                        }
                    }
                    b = b->next;
                }
                if (count == 0) printf("  None\n");
            } else if (strcmp(line, "/compact") == 0) {
                compact_arena();
                sync_soul_file(soul_filename);
            } else if (strcmp(line, "/soul") == 0) {
                struct stat st;
                if (stat(soul_filename, &st) == 0) {
                    char timebuf[128];
                    struct tm *tm_info = localtime(&st.st_mtime);
                    strftime(timebuf, sizeof(timebuf), "%c", tm_info);
                    printf("Soul file: %s (%ld bytes, last modified: %s\n", soul_filename, st.st_size, timebuf);
                } else {
                    printf("Soul file not found.\n");
                }
            } else if (strcmp(line, "/reconnect") == 0) {
                if (ollama_healthy()) {
                    no_llm_mode = 0;
                    printf("Reconnected to Ollama. LLM mode enabled.\n");
                } else {
                    printf("Ollama still unreachable.\n");
                }
            } else {
                // Fallback to interpreter for other slash commands
                char *resp = interpret_command(line);
                printf("%s\n", resp);
                free(resp);
            }
            if (dirty) {
                save_state(state_filename);
                sync_soul_file(soul_filename);
                dirty = 0;
            }
            continue;
        }

        if (no_llm_mode) {
            char *resp = interpret_command(line);
            printf(COLOR_BLUE "%s\n" COLOR_RESET, resp);
            free(resp);
            continue;
        }

        blob_append(BLOB_USER, line);
        sync_soul_file(soul_filename);
        int max_iter = 3;
        char *last_response = NULL;
        for (int iter = 0; iter < max_iter; iter++) {
            char *prompt = build_simple_prompt(line);
            char *llm_out = call_llm(prompt);
            free(prompt);
            if (!llm_out) {
                printf("LLM call failed. Try again or use /reconnect.\n");
                break;
            }
            char *tool_json = extract_tool_json(llm_out);
            if (!tool_json) {
                // final response
                printf(COLOR_BLUE "Assistant: %s\n" COLOR_RESET, llm_out);
                blob_append(BLOB_ASSISTANT, llm_out);
                last_response = llm_out;
                break;
            }
            char *tool_name = NULL, *tool_args = NULL;
            if (parse_tool_call(tool_json, &tool_name, &tool_args)) {
                if (dry_run) {
                    printf(COLOR_YELLOW "[DRY RUN] would execute %s(%s)\n" COLOR_RESET, tool_name, tool_args);
                } else {
                    char *result = execute_tool(tool_name, tool_args);
                    printf(COLOR_YELLOW "\n[Tool %s] → %s\n" COLOR_RESET, tool_name, result);
                    blob_append(BLOB_TOOL_CALL, tool_json);
                    blob_append(BLOB_TOOL_RESULT, result);
                    trigger_webhooks(tool_name, result);
                    // feed result back to LLM and continue loop
                    char *next_prompt;
                    asprintf(&next_prompt, "Tool result: %s\nContinue.", result);
                    free(llm_out);
                    llm_out = call_llm(next_prompt);
                    free(next_prompt);
                    free(result);
                    if (!llm_out) break;
                    // use llm_out as next response candidate, loop continues
                    free(tool_json);
                    free(tool_name);
                    free(tool_args);
                    continue;
                }
            } else {
                printf(COLOR_RED "Invalid tool JSON: %s\n" COLOR_RESET, tool_json);
                blob_append(BLOB_ASSISTANT, llm_out);
                last_response = llm_out;
            }
            free(tool_json);
            free(tool_name);
            free(tool_args);
            break;
        }
        if (last_response) free(last_response);
        save_state(state_filename);
        sync_soul_file(soul_filename);
    }

    save_state(state_filename);
    curl_global_cleanup();
    return 0;
}

// shadowclaw.c - Fixed and Enhanced Version
// Compile with: gcc -D_GNU_SOURCE -DUSE_READLINE -Wall -Wextra -O2 shadowclaw.c cJSON.c interpreter.c -o shadowclaw -lcurl -lpthread -lreadline -lm

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <ctype.h>
#include <signal.h>
#include <locale.h>
#include "cJSON.h"
#include "interpreter.h"

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

// ------------------------------------------------------------------
// Version
// ------------------------------------------------------------------
#define SHADOWCLAW_VERSION "3.2"   // enhanced with colors & readline

// ------------------------------------------------------------------
// ANSI color codes
// ------------------------------------------------------------------
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// ------------------------------------------------------------------
// Global state (shared between threads)
// ------------------------------------------------------------------
static void* shadow_global = NULL;
static pthread_mutex_t arena_mutex = PTHREAD_MUTEX_INITIALIZER;
static char state_filename[PATH_MAX] = "shadowclaw.bin";
static char soul_dir[PATH_MAX] = "shadowclaw_data";
static char soul_file[PATH_MAX] = "shadowclaw_data/shadowsoul.md";

// ------------------------------------------------------------------
// Command-line flags
// ------------------------------------------------------------------
static int dry_run = 0;
static FILE* log_file = NULL;

// ------------------------------------------------------------------
// Shadow arena header (now includes next_id)
// ------------------------------------------------------------------
typedef struct ShadowHeader {
    size_t capacity;
    size_t used;
    unsigned int magic;
    uint32_t crc;
    uint64_t next_id;           // persisted
} ShadowHeader;

#define SHADOW_MAGIC 0xDEADBEEF
#define HEADER_SIZE (sizeof(ShadowHeader))

static void* shadow_alloc(void* ptr, size_t old_size, size_t new_size) {
    (void)old_size;
    return realloc(ptr, new_size);
}

static void* shadow_malloc(size_t size) {
    return malloc(size);
}

static void shadow_free(void* ptr) {
    free(ptr);
}

// ------------------------------------------------------------------
// CRC32 implementation (full table)
// ------------------------------------------------------------------
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b4c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
        0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };
    crc ^= 0xffffffff;
    while (len--) {
        crc = (crc >> 8) ^ table[(crc ^ *data++) & 0xff];
    }
    return crc ^ 0xffffffff;
}

static uint32_t compute_arena_crc(void* arena) {
    if (!arena) return 0;
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    return crc32_update(0, (uint8_t*)arena, hdr->used);
}

// ------------------------------------------------------------------
// Arena functions (complete)
// ------------------------------------------------------------------
static void* arena_grow(void* arena, size_t new_cap) {
    if (new_cap > SIZE_MAX - HEADER_SIZE) {
        fprintf(stderr, COLOR_RED "arena_grow: requested capacity too large\n" COLOR_RESET);
        return NULL;
    }
    if (!arena) {
        size_t total = HEADER_SIZE + new_cap;
        ShadowHeader* hdr = (ShadowHeader*)shadow_malloc(total);
        if (!hdr) return NULL;
        hdr->capacity = new_cap;
        hdr->used = 0;
        hdr->magic = SHADOW_MAGIC;
        hdr->crc = 0;
        hdr->next_id = 1;
        return (char*)hdr + HEADER_SIZE;
    }
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    if (hdr->magic != SHADOW_MAGIC) return NULL;
    size_t total = HEADER_SIZE + new_cap;
    ShadowHeader* new_hdr = (ShadowHeader*)shadow_alloc(hdr, HEADER_SIZE + hdr->capacity, total);
    if (!new_hdr) {
        size_t smaller_cap = hdr->capacity + (hdr->capacity / 4);
        if (smaller_cap < new_cap) {
            total = HEADER_SIZE + smaller_cap;
            new_hdr = (ShadowHeader*)shadow_alloc(hdr, HEADER_SIZE + hdr->capacity, total);
        }
        if (!new_hdr) return NULL;
        new_hdr->capacity = smaller_cap;
    } else {
        new_hdr->capacity = new_cap;
    }
    return (char*)new_hdr + HEADER_SIZE;
}

static void* arena_alloc(void* arena, size_t size) {
    if (!arena) return NULL;
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    if (hdr->used + size > hdr->capacity) return NULL;
    void* ptr = (char*)arena + hdr->used;
    hdr->used += size;
    return ptr;
}

static void* arena_ensure(void* arena, size_t needed) {
    if (!arena) return NULL;
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    if (hdr->used + needed <= hdr->capacity) return arena;

    size_t new_cap;
    if (hdr->capacity > SIZE_MAX / 2) {
        new_cap = hdr->used + needed + 1024;
        if (new_cap < hdr->used + needed) {
            fprintf(stderr, COLOR_RED "arena_ensure: needed size too large\n" COLOR_RESET);
            return NULL;
        }
    } else {
        new_cap = hdr->capacity * 2;
        if (new_cap < hdr->used + needed) {
            new_cap = hdr->used + needed + 1024;
            if (new_cap < hdr->used + needed) {
                fprintf(stderr, COLOR_RED "arena_ensure: needed size too large\n" COLOR_RESET);
                return NULL;
            }
        }
    }

    if (new_cap > SIZE_MAX - HEADER_SIZE) {
        fprintf(stderr, COLOR_RED "arena_ensure: new capacity would overflow\n" COLOR_RESET);
        return NULL;
    }
    void* new_arena = arena_grow(arena, new_cap);
    if (!new_arena) {
        fprintf(stderr, COLOR_RED "Arena growth failed: needed %zu, current capacity %zu\n" COLOR_RESET,
                hdr->used + needed, hdr->capacity);
    }
    return new_arena;
}

// ------------------------------------------------------------------
// Blob kinds (added CORE_MEMORY)
// ------------------------------------------------------------------
typedef enum {
    BLOB_KIND_SYSTEM = 0,
    BLOB_KIND_USER,
    BLOB_KIND_ASSISTANT,
    BLOB_KIND_TOOL_CALL,
    BLOB_KIND_TOOL_RESULT,
    BLOB_KIND_WEBHOOK,
    BLOB_KIND_CRONJOB,
    BLOB_KIND_SKILL,
    BLOB_KIND_CORE_MEMORY,
    BLOB_KIND_DELETED = 255
} BlobKind;

typedef struct BlobHeader {
    size_t size;
    BlobKind kind;
    uint64_t id;
} BlobHeader;

#define BLOB_HEADER_SIZE (sizeof(BlobHeader))

static void* blob_append(void** arena_ptr, BlobKind kind, const char* data) {
    if (!arena_ptr || !*arena_ptr || !data) return NULL;
    void* arena = *arena_ptr;
    size_t len = strlen(data) + 1;
    if (len > SIZE_MAX - BLOB_HEADER_SIZE) {
        fprintf(stderr, COLOR_RED "blob_append: data too large\n" COLOR_RESET);
        return NULL;
    }
    size_t total = BLOB_HEADER_SIZE + len;
    arena = arena_ensure(arena, total);
    if (!arena) return NULL;
    void* ptr = arena_alloc(arena, total);
    if (!ptr) return NULL;
    BlobHeader* hdr = (BlobHeader*)ptr;
    hdr->size = len;
    hdr->kind = kind;
    ShadowHeader* shadow = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    hdr->id = shadow->next_id++;
    char* blob_data = (char*)ptr + BLOB_HEADER_SIZE;
    memcpy(blob_data, data, len);
    *arena_ptr = arena;
    return hdr;
}

static BlobHeader* blob_iterate(void* arena, BlobHeader* prev) {
    if (!arena) return NULL;
    ShadowHeader* shadow = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    if (!prev) {
        if (shadow->used < BLOB_HEADER_SIZE) return NULL;
        BlobHeader* first = (BlobHeader*)arena;
        while (first && first->kind == BLOB_KIND_DELETED) {
            char* next = (char*)first + BLOB_HEADER_SIZE + first->size;
            if (next + BLOB_HEADER_SIZE > (char*)arena + shadow->used) break;
            first = (BlobHeader*)next;
        }
        if (first && first->kind == BLOB_KIND_DELETED) return NULL;
        return first;
    }
    char* end = (char*)arena + shadow->used;
    char* next = (char*)prev + BLOB_HEADER_SIZE + prev->size;
    while (next + BLOB_HEADER_SIZE <= end) {
        BlobHeader* candidate = (BlobHeader*)next;
        if (candidate->kind != BLOB_KIND_DELETED) {
            return candidate;
        }
        next = (char*)candidate + BLOB_HEADER_SIZE + candidate->size;
    }
    return NULL;
}

// ------------------------------------------------------------------
// Compact arena (rewrites live blobs, preserves next_id)
// ------------------------------------------------------------------
static void* compact_arena(void* old_arena) {
    if (!old_arena) return NULL;
    ShadowHeader* old_hdr = (ShadowHeader*)((char*)old_arena - HEADER_SIZE);
    size_t live_used = 0;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(old_arena, blob)) != NULL) {
        live_used += BLOB_HEADER_SIZE + blob->size;
    }
    if (live_used == 0) {
        old_hdr->used = 0;
        return old_arena;
    }
    size_t new_cap = old_hdr->capacity;
    if (new_cap < live_used) new_cap = live_used + 1024;
    void* new_arena = arena_grow(NULL, new_cap);
    if (!new_arena) return NULL;

    blob = NULL;
    while ((blob = blob_iterate(old_arena, blob)) != NULL) {
        size_t total = BLOB_HEADER_SIZE + blob->size;
        void* ptr = arena_alloc(new_arena, total);
        if (!ptr) {
            shadow_free((char*)new_arena - HEADER_SIZE);
            return NULL;
        }
        memcpy(ptr, blob, total);
    }
    ShadowHeader* new_hdr = (ShadowHeader*)((char*)new_arena - HEADER_SIZE);
    new_hdr->next_id = old_hdr->next_id;
    shadow_free(old_hdr);
    return new_arena;
}

static void maybe_compact(void** arena_ptr) {
    void* arena = *arena_ptr;
    if (!arena) return;
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    size_t deleted_total = 0;
    char* pos = (char*)arena;
    char* end = pos + hdr->used;
    while (pos + BLOB_HEADER_SIZE <= end) {
        BlobHeader* blob = (BlobHeader*)pos;
        if (blob->kind == BLOB_KIND_DELETED) {
            deleted_total += BLOB_HEADER_SIZE + blob->size;
        }
        pos += BLOB_HEADER_SIZE + blob->size;
    }
    if (deleted_total > hdr->used / 5) {
        void* new_arena = compact_arena(arena);
        if (new_arena) *arena_ptr = new_arena;
    }
}

// ------------------------------------------------------------------
// Persistence (save/load) - FIXED: pointer-to-pointer
// ------------------------------------------------------------------
static int save_state(void** arena_ptr, const char* filename) {
    if (!arena_ptr || !*arena_ptr) return -1;
    maybe_compact(arena_ptr);
    void* arena = *arena_ptr;
    ShadowHeader* hdr = (ShadowHeader*)((char*)arena - HEADER_SIZE);
    hdr->crc = compute_arena_crc(arena);

    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, COLOR_RED "Failed to open %s for writing: %s\n" COLOR_RESET, filename, strerror(errno));
        return -1;
    }
    size_t total = HEADER_SIZE + hdr->used;
    size_t written = 0;
    while (written < total) {
        size_t ret = fwrite((char*)hdr + written, 1, total - written, f);
        if (ret == 0) {
            fprintf(stderr, COLOR_RED "Failed to write state to %s (disk full?)\n" COLOR_RESET, filename);
            fclose(f);
            return -1;
        }
        written += ret;
    }
    fclose(f);
    return 0;
}

static void* load_state(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        if (errno != ENOENT) {
            fprintf(stderr, COLOR_RED "Failed to open %s for reading: %s\n" COLOR_RESET, filename, strerror(errno));
        }
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return NULL;
    }
    long total = ftell(f);
    if (total == -1L) {
        perror("ftell");
        fclose(f);
        return NULL;
    }
    if (total < (long)HEADER_SIZE) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    ShadowHeader* hdr = (ShadowHeader*)shadow_malloc(total);
    if (!hdr) {
        fclose(f);
        return NULL;
    }
    if (fread(hdr, 1, total, f) != (size_t)total) {
        shadow_free(hdr);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (hdr->magic != SHADOW_MAGIC || hdr->used > hdr->capacity) {
        shadow_free(hdr);
        return NULL;
    }
    void* arena = (char*)hdr + HEADER_SIZE;
    uint32_t expected_crc = hdr->crc;
    hdr->crc = 0;
    uint32_t actual_crc = compute_arena_crc(arena);
    if (actual_crc != expected_crc) {
        fprintf(stderr, COLOR_RED "State file corrupted (CRC mismatch). Attempting to continue, but data may be lost.\n" COLOR_RESET);
    }
    hdr->crc = expected_crc;
    return arena;
}

// ------------------------------------------------------------------
// Soul file management (FIXED: unlock on early returns)
// ------------------------------------------------------------------
static void ensure_soul_directory(void) {
    struct stat st = {0};
    if (stat(soul_dir, &st) == -1) {
        mkdir(soul_dir, 0700);
    }
}

static int sync_counter = 0;
static void sync_soul_file(void) {
    sync_counter++;
    if (sync_counter % 5 != 0) return;

    pthread_mutex_lock(&arena_mutex);
    void* arena = shadow_global;
    if (!arena) {
        pthread_mutex_unlock(&arena_mutex);
        return;
    }

    FILE* f = fopen(soul_file, "w");
    if (!f) {
        fprintf(stderr, COLOR_RED "Failed to open soul file for writing: %s\n" COLOR_RESET, strerror(errno));
        pthread_mutex_unlock(&arena_mutex);
        return;
    }

    fprintf(f, "# ShadowSoul - Unified Memory\n");
    time_t now = time(NULL);
    struct tm tm_buf;
    char timestr[128];
    localtime_r(&now, &tm_buf);
    strftime(timestr, sizeof(timestr), "%c", &tm_buf);
    fprintf(f, "Last updated: %s\n\n", timestr);

    fprintf(f, "## Core Memory\n");
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(arena, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CORE_MEMORY) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            fprintf(f, "```json\n%s\n```\n\n", data);
            break;
        }
    }

    fprintf(f, "## Skills\n");
    blob = NULL;
    while ((blob = blob_iterate(arena, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_SKILL) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            fprintf(f, "- %s\n", data);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "## Cron Jobs\n");
    blob = NULL;
    while ((blob = blob_iterate(arena, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CRONJOB) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            fprintf(f, "- %s\n", data);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "## Webhooks\n");
    blob = NULL;
    while ((blob = blob_iterate(arena, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_WEBHOOK) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            fprintf(f, "- %s\n", data);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "## Conversation Log\n");
    blob = NULL;
    while ((blob = blob_iterate(arena, blob)) != NULL) {
        const char* data = (const char*)blob + BLOB_HEADER_SIZE;
        const char* kind_str = "";
        switch (blob->kind) {
            case BLOB_KIND_SYSTEM:    kind_str = "System"; break;
            case BLOB_KIND_USER:      kind_str = "User"; break;
            case BLOB_KIND_ASSISTANT: kind_str = "Assistant"; break;
            case BLOB_KIND_TOOL_CALL: kind_str = "Tool Call"; break;
            case BLOB_KIND_TOOL_RESULT: kind_str = "Tool Result"; break;
            default: continue;
        }
        fprintf(f, "### %s\n%s\n\n", kind_str, data);
    }

    fclose(f);
    pthread_mutex_unlock(&arena_mutex);
}

// ------------------------------------------------------------------
// Safe math (locale-independent)
// ------------------------------------------------------------------
static double eval_expr(const char** expr, int* error);
static double eval_term(const char** expr, int* error);
static double eval_factor(const char** expr, int* error);

static double eval_factor(const char** expr, int* error) {
    while (**expr == ' ') (*expr)++;
    double result;
    if (**expr == '(') {
        (*expr)++;
        result = eval_expr(expr, error);
        if (**expr != ')') *error = 1;
        else (*expr)++;
    } else if (**expr >= '0' && **expr <= '9') {
        char* end;
        result = strtod(*expr, &end);
        *expr = end;
    } else {
        *error = 1;
        result = 0;
    }
    while (**expr == ' ') (*expr)++;
    return result;
}

static double eval_term(const char** expr, int* error) {
    double left = eval_factor(expr, error);
    while (!*error && (**expr == '*' || **expr == '/')) {
        char op = **expr;
        (*expr)++;
        double right = eval_factor(expr, error);
        if (op == '*') left *= right;
        else {
            if (right == 0) *error = 1;
            else left /= right;
        }
    }
    return left;
}

static double eval_expr(const char** expr, int* error) {
    double left = eval_term(expr, error);
    while (!*error && (**expr == '+' || **expr == '-')) {
        char op = **expr;
        (*expr)++;
        double right = eval_term(expr, error);
        if (op == '+') left += right;
        else left -= right;
    }
    return left;
}

static char* safe_math(const char* input) {
    const char* p = input;
    int error = 0;
    // Force C locale for number parsing
    char* old_locale = strdup(setlocale(LC_NUMERIC, NULL));
    setlocale(LC_NUMERIC, "C");
    double result = eval_expr(&p, &error);
    setlocale(LC_NUMERIC, old_locale);
    free(old_locale);
    if (error || *p != '\0') {
        return strdup("Invalid arithmetic expression");
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", result);
    return strdup(buf);
}

// ------------------------------------------------------------------
// Domain allowlist and safe path (FIXED)
// ------------------------------------------------------------------
static const char* allowed_domains[] = {
    "example.com",
    "api.example.com",
    "localhost",
    NULL
};

static int is_domain_allowed(const char* host) {
    for (int i = 0; allowed_domains[i] != NULL; i++) {
        if (strcmp(host, allowed_domains[i]) == 0) return 1;
    }
    return 0;
}

static char* safe_path(const char* user_path, const char* base_dir) {
    char resolved_base[PATH_MAX];
    if (!realpath(base_dir, resolved_base)) return NULL;
    char resolved_user[PATH_MAX];
    if (realpath(user_path, resolved_user)) {
        if (strncmp(resolved_user, resolved_base, strlen(resolved_base)) != 0)
            return NULL;
        return strdup(resolved_user);
    }
    // If user_path does not exist, try to resolve its parent directory
    char* dir = strdup(user_path);
    if (!dir) return NULL;
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (realpath(dir, resolved_base)) {
            snprintf(resolved_user, sizeof(resolved_user), "%s/%s", resolved_base, last_slash+1);
            if (strncmp(resolved_user, resolved_base, strlen(resolved_base)) == 0) {
                free(dir);
                return strdup(resolved_user);
            }
        }
    }
    free(dir);
    return NULL;
}

// ------------------------------------------------------------------
// Tool system
// ------------------------------------------------------------------
typedef struct Tool {
    const char* name;
    char* (*fn)(const char*);
    const char* description;
    const char* parameters;
    const char* example;
} Tool;

#define MAX_FILE_SIZE (10 * 1024 * 1024)
#define MAX_HTTP_SIZE (10 * 1024 * 1024)
#define MAX_TOOL_RESULT_LEN 2000

static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    char** response_ptr = (char**)userdata;
    size_t current_len = *response_ptr ? strlen(*response_ptr) : 0;
    char* new = realloc(*response_ptr, current_len + total + 1);
    if (!new) return 0;
    *response_ptr = new;
    memcpy(*response_ptr + current_len, ptr, total);
    (*response_ptr)[current_len + total] = '\0';
    return total;
}

static size_t discard_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    (void)ptr; (void)userdata;
    return size * nmemb;
}

static int http_post(const char* url, const char* body) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK) {
        fprintf(stderr, COLOR_RED "Webhook POST failed: %s\n" COLOR_RESET, curl_easy_strerror(res));
    }
    return (res == CURLE_OK) ? 0 : -1;
}

#ifdef ENABLE_SHELL_TOOL
static char* tool_shell(const char* args) {
    if (dry_run) return strdup("[DRY RUN] shell would execute");
    for (const char* p = args; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == ' ' || *p == '-' || *p == '_' || *p == '.' || *p == '/')) {
            return strdup("Invalid characters in command");
        }
    }
    FILE* fp = popen(args, "r");
    if (!fp) return strdup("Failed to execute command");
    char* result = NULL;
    size_t total = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        char* new_result = realloc(result, total + len + 1);
        if (!new_result) {
            free(result);
            pclose(fp);
            return strdup("Memory error");
        }
        result = new_result;
        memcpy(result + total, buf, len + 1);
        total += len;
    }
    pclose(fp);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT_LEN) result[MAX_TOOL_RESULT_LEN] = '\0';
    return result;
}
#else
static char* tool_shell(const char* args) {
    (void)args;
    return strdup("Shell tool is disabled for security reasons.");
}
#endif

static char* tool_file_read(const char* args) {
    if (dry_run) return strdup("[DRY RUN] file_read would read");
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return strdup("Cannot determine current directory");
    char* safe = safe_path(args, cwd);
    if (!safe) return strdup("Path not allowed (must be within current directory)");
    FILE* f = fopen(safe, "rb");
    free(safe);
    if (!f) return strdup("File not found");
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return strdup("Error seeking file"); }
    long size = ftell(f);
    if (size == -1) { fclose(f); return strdup("Error getting file size"); }
    if (size > MAX_FILE_SIZE) { fclose(f); return strdup("File too large (max 10 MB)"); }
    rewind(f);
    char* content = malloc(size + 1);
    if (!content) { fclose(f); return strdup("Memory error"); }
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);
    if (strlen(content) > MAX_TOOL_RESULT_LEN) content[MAX_TOOL_RESULT_LEN] = '\0';
    return content;
}

static char* tool_file_write(const char* args) {
    if (dry_run) return strdup("[DRY RUN] file_write would write");
    const char* space = strchr(args, ' ');
    if (!space) return strdup("Invalid format: need 'filename content'");
    size_t filename_len = space - args;
    char* filename = malloc(filename_len + 1);
    if (!filename) return strdup("Memory error");
    memcpy(filename, args, filename_len);
    filename[filename_len] = '\0';

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) { free(filename); return strdup("Cannot determine current directory"); }
    char* safe = safe_path(filename, cwd);
    free(filename);
    if (!safe) return strdup("Path not allowed (must be within current directory)");

    const char* content = space + 1;
    FILE* f = fopen(safe, "w");
    if (!f) { free(safe); return strdup("Cannot write file"); }
    fprintf(f, "%s", content);
    fclose(f);
    free(safe);
    return strdup("File written");
}

static char* tool_http_get(const char* args) {
    if (dry_run) return strdup("[DRY RUN] http_get would fetch");
    const char* host_start = args;
    if (strncmp(args, "http://", 7) == 0) host_start += 7;
    else if (strncmp(args, "https://", 8) == 0) host_start += 8;
    else return strdup("Only http:// and https:// URLs are allowed");

    const char* host_end = strchr(host_start, '/');
    size_t host_len = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
    char host[256];
    if (host_len >= sizeof(host)) return strdup("Hostname too long");
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    if (!is_domain_allowed(host)) return strdup("Domain not allowed");

    CURL* curl = curl_easy_init();
    if (!curl) return strdup("curl init failed");
    char* response = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, args);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, MAX_HTTP_SIZE);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(response); return strdup("HTTP request failed"); }
    if (response && strlen(response) > MAX_TOOL_RESULT_LEN) response[MAX_TOOL_RESULT_LEN] = '\0';
    return response ? response : strdup("");
}

static char* tool_math(const char* args) {
    if (dry_run) return strdup("[DRY RUN] math would compute");
    return safe_math(args);
}

static char* tool_list_dir(const char* args) {
    if (dry_run) return strdup("[DRY RUN] list_dir would list");
    DIR* d = opendir(args);
    if (!d) return strdup("Error: cannot open directory");
    char* result = NULL;
    size_t len = 0;
    FILE* out = open_memstream(&result, &len);
    if (!out) { closedir(d); return strdup("Memory error"); }
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) fprintf(out, "%s\n", entry->d_name);
    closedir(d);
    fclose(out);
    if (!result) result = strdup("");
    if (strlen(result) > MAX_TOOL_RESULT_LEN) result[MAX_TOOL_RESULT_LEN] = '\0';
    return result;
}

static char* build_json_object(const char* key1, const char* val1,
                               const char* key2, const char* val2,
                               const char* key3, const char* val3,
                               const char* key4, const char* val4) {
    cJSON* obj = cJSON_CreateObject();
    if (key1 && val1) cJSON_AddStringToObject(obj, key1, val1);
    if (key2 && val2) cJSON_AddStringToObject(obj, key2, val2);
    if (key3 && val3) cJSON_AddStringToObject(obj, key3, val3);
    if (key4 && val4) cJSON_AddStringToObject(obj, key4, val4);
    char* str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return str;
}

static char* tool_webhook_add(const char* args) {
    if (dry_run) return strdup("[DRY RUN] webhook_add would add");
    cJSON* json = cJSON_Parse(args);
    if (!json) return strdup("Invalid JSON");
    cJSON* url = cJSON_GetObjectItem(json, "url");
    cJSON* event = cJSON_GetObjectItem(json, "event");
    if (!cJSON_IsString(url) || !cJSON_IsString(event)) {
        cJSON_Delete(json);
        return strdup("Missing url or event");
    }
    char* entry = build_json_object("url", url->valuestring,
                                     "event", event->valuestring,
                                     NULL, NULL, NULL, NULL);
    cJSON_Delete(json);
    return entry ? entry : strdup("Memory error");
}

static char* tool_cron_add(const char* args) {
    if (dry_run) return strdup("[DRY RUN] cron_add would add");
    cJSON* json = cJSON_Parse(args);
    if (!json) return strdup("Invalid JSON");
    cJSON* schedule = cJSON_GetObjectItem(json, "schedule");
    cJSON* tool = cJSON_GetObjectItem(json, "tool");
    cJSON* tool_args = cJSON_GetObjectItem(json, "args");
    if (!cJSON_IsString(schedule) || !cJSON_IsString(tool) || !cJSON_IsString(tool_args)) {
        cJSON_Delete(json);
        return strdup("Missing schedule, tool, or args");
    }
    char* entry = build_json_object("schedule", schedule->valuestring,
                                     "tool", tool->valuestring,
                                     "args", tool_args->valuestring,
                                     "last_run", "0");
    cJSON_Delete(json);
    return entry ? entry : strdup("Memory error");
}

static char* tool_heartbeat(const char* args) {
    (void)args;
    return strdup("Heartbeat");
}

static char* tool_cron_list(const char* args) {
    (void)args;
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    char* result = NULL;
    size_t len = 0;
    FILE* out = open_memstream(&result, &len);
    if (out) {
        BlobHeader* blob = NULL;
        while ((blob = blob_iterate(shadow, blob)) != NULL) {
            if (blob->kind == BLOB_KIND_CRONJOB) {
                const char* data = (const char*)blob + BLOB_HEADER_SIZE;
                fprintf(out, "%s\n", data);
            }
        }
        fclose(out);
    }
    pthread_mutex_unlock(&arena_mutex);
    if (result && strlen(result) > MAX_TOOL_RESULT_LEN) result[MAX_TOOL_RESULT_LEN] = '\0';
    return result ? result : strdup("");
}

static char* tool_cron_remove(const char* args) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    int removed = 0;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CRONJOB) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            if (strstr(data, args) != NULL) {
                blob->kind = BLOB_KIND_DELETED;
                removed++;
            }
        }
    }
    pthread_mutex_unlock(&arena_mutex);
    char msg[64];
    snprintf(msg, sizeof(msg), "Removed %d cron job(s)", removed);
    return strdup(msg);
}

static char* tool_skill_add(const char* args) {
    if (dry_run) return strdup("[DRY RUN] skill_add would add");
    cJSON* json = cJSON_Parse(args);
    if (!json) return strdup("Invalid JSON");
    cJSON* name = cJSON_GetObjectItem(json, "name");
    cJSON* desc = cJSON_GetObjectItem(json, "desc");
    cJSON* steps = cJSON_GetObjectItem(json, "steps");
    cJSON* interpreter_cmd = cJSON_GetObjectItem(json, "interpreter_command");
    if (!cJSON_IsString(name) || !cJSON_IsString(desc) || !cJSON_IsArray(steps)) {
        cJSON_Delete(json);
        return strdup("Missing name, desc, or steps (array)");
    }
    char* steps_str = cJSON_PrintUnformatted(steps);
    char* entry;
    if (cJSON_IsString(interpreter_cmd)) {
        entry = build_json_object("name", name->valuestring,
                                  "desc", desc->valuestring,
                                  "steps", steps_str,
                                  "interpreter_command", interpreter_cmd->valuestring);
    } else {
        entry = build_json_object("name", name->valuestring,
                                  "desc", desc->valuestring,
                                  "steps", steps_str,
                                  NULL, NULL);
    }
    free(steps_str);
    cJSON_Delete(json);
    if (!entry) return strdup("Memory error");
    return entry;
}

static char* tool_skill_run(const char* args) {
    if (dry_run) return strdup("[DRY RUN] skill_run would execute");
    char* copy = strdup(args);
    if (!copy) return strdup("Memory error");
    char* space = strchr(copy, ' ');
    if (space) *space = '\0';
    const char* skill_name = copy;
    const char* skill_args = space ? space + 1 : "";
    char* result;
    int needed = snprintf(NULL, 0, "RUN_SKILL:%s:%s", skill_name, skill_args);
    if (needed < 0) { free(copy); return strdup("Formatting error"); }
    result = malloc(needed + 1);
    if (!result) { free(copy); return strdup("Memory error"); }
    snprintf(result, needed + 1, "RUN_SKILL:%s:%s", skill_name, skill_args);
    free(copy);
    return result;
}

static char* tool_list_skills(const char* args) {
    (void)args;
    return strdup("LIST_SKILLS");
}

static char* tool_update_core_memory(const char* args) {
    cJSON* updates = cJSON_Parse(args);
    if (!updates) return strdup("Invalid JSON");
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    BlobHeader* core_blob = NULL;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CORE_MEMORY) {
            core_blob = blob;
            break;
        }
    }
    cJSON* core_json = NULL;
    if (core_blob) {
        const char* core_data = (const char*)core_blob + BLOB_HEADER_SIZE;
        core_json = cJSON_Parse(core_data);
    }
    if (!core_json) core_json = cJSON_CreateObject();

    cJSON* child = NULL;
    cJSON_ArrayForEach(child, updates) {
        cJSON_AddItemToObject(core_json, child->string, cJSON_Duplicate(child, 1));
    }
    char* new_core_str = cJSON_PrintUnformatted(core_json);
    cJSON_Delete(core_json);
    cJSON_Delete(updates);

    if (core_blob) core_blob->kind = BLOB_KIND_DELETED;
    if (!blob_append(&shadow_global, BLOB_KIND_CORE_MEMORY, new_core_str)) {
        free(new_core_str);
        pthread_mutex_unlock(&arena_mutex);
        return strdup("Failed to update core memory");
    }
    free(new_core_str);
    pthread_mutex_unlock(&arena_mutex);
    sync_soul_file();
    return strdup("Core memory updated");
}

static char* tool_recall(const char* args) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    char* result = NULL;
    size_t len = 0;
    FILE* out = open_memstream(&result, &len);
    if (out) {
        BlobHeader* blob = NULL;
        while ((blob = blob_iterate(shadow, blob)) != NULL) {
            if (blob->kind == BLOB_KIND_USER ||
                blob->kind == BLOB_KIND_ASSISTANT ||
                blob->kind == BLOB_KIND_TOOL_CALL ||
                blob->kind == BLOB_KIND_TOOL_RESULT) {
                const char* data = (const char*)blob + BLOB_HEADER_SIZE;
                if (strcasestr(data, args)) {
                    fprintf(out, "[%s] %s\n",
                            blob->kind == BLOB_KIND_USER ? "User" :
                            blob->kind == BLOB_KIND_ASSISTANT ? "Assistant" :
                            blob->kind == BLOB_KIND_TOOL_CALL ? "Tool call" : "Tool result",
                            data);
                }
            }
        }
        fclose(out);
    }
    pthread_mutex_unlock(&arena_mutex);
    if (result && strlen(result) > MAX_TOOL_RESULT_LEN) result[MAX_TOOL_RESULT_LEN] = '\0';
    return result ? result : strdup("No matches");
}

// ------------------------------------------------------------------
// Tool table
// ------------------------------------------------------------------
static Tool tools[] = {
    {"shell", tool_shell, "Execute a shell command (disabled by default).", "{\"type\":\"string\"}", "{\"tool\":\"shell\", \"args\":\"ls -l\"}"},
    {"file_read", tool_file_read, "Read a file (max 10 MB).", "{\"type\":\"string\"}", "{\"tool\":\"file_read\", \"args\":\"notes.txt\"}"},
    {"file_write", tool_file_write, "Write content to a file.", "{\"type\":\"string\"}", "{\"tool\":\"file_write\", \"args\":[\"output.txt\", \"Hello world\"]}"},
    {"http_get", tool_http_get, "HTTP GET to allowed domain.", "{\"type\":\"string\"}", "{\"tool\":\"http_get\", \"args\":\"https://example.com\"}"},
    {"math", tool_math, "Evaluate arithmetic expression.", "{\"type\":\"string\"}", "{\"tool\":\"math\", \"args\":\"(2+3)*4\"}"},
    {"list_dir", tool_list_dir, "List directory contents.", "{\"type\":\"string\"}", "{\"tool\":\"list_dir\", \"args\":\".\"}"},
    {"webhook_add", tool_webhook_add, "Register a webhook.", "{\"type\":\"object\"}", "{\"tool\":\"webhook_add\", \"args\":{\"url\":\"http://example.com/hook\",\"event\":\"tool:http_get\"}}"},
    {"cron_add", tool_cron_add, "Add a cron job.", "{\"type\":\"object\"}", "{\"tool\":\"cron_add\", \"args\":{\"schedule\":\"@every 30s\",\"tool\":\"math\",\"args\":\"1+1\"}}"},
    {"cron_list", tool_cron_list, "List all cron jobs.", "{}", "{\"tool\":\"cron_list\"}"},
    {"cron_remove", tool_cron_remove, "Remove cron jobs by substring.", "{\"type\":\"string\"}", "{\"tool\":\"cron_remove\", \"args\":\"@every\"}"},
    {"skill_add", tool_skill_add, "Create a dynamic skill.", "{\"type\":\"object\"}", "{\"tool\":\"skill_add\", \"args\":{\"name\":\"weather\",\"desc\":\"Get weather\",\"steps\":[{\"tool\":\"http_get\",\"args\":\"https://wttr.in\"}]}}"},
    {"skill_run", tool_skill_run, "Run a skill.", "{\"type\":\"string\"}", "{\"tool\":\"skill_run\", \"args\":\"weather London\"}"},
    {"list_skills", tool_list_skills, "List all skills.", "{}", "{\"tool\":\"list_skills\"}"},
    {"update_core_memory", tool_update_core_memory, "Update core memory.", "{\"type\":\"object\"}", "{\"tool\":\"update_core_memory\", \"args\":{\"user_name\":\"Alice\"}}"},
    {"recall", tool_recall, "Search conversation history.", "{\"type\":\"string\"}", "{\"tool\":\"recall\", \"args\":\"project\"}"},
    {"heartbeat", tool_heartbeat, "Internal heartbeat tool.", "{}", "{\"tool\":\"heartbeat\"}"},
    {NULL, NULL, NULL, NULL, NULL}
};

static int validate_tool_call(const char *tool_name, const char *args) {
    (void)args;
    for (Tool *t = tools; t->name; t++) {
        if (strcmp(t->name, tool_name) == 0) {
#ifdef ENABLE_SHELL_TOOL
            if (strcmp(tool_name, "shell") == 0) return 1;
#else
            if (strcmp(tool_name, "shell") == 0) return 0;
#endif
            return 1;
        }
    }
    return 0;
}

static char* execute_tool(const char* name, const char* args) {
    for (Tool* t = tools; t->name; t++) {
        if (strcmp(t->name, name) == 0) {
            char* result = t->fn(args);
            if (result && strlen(result) > MAX_TOOL_RESULT_LEN) result[MAX_TOOL_RESULT_LEN] = '\0';
            return result;
        }
    }
    return strdup("Unknown tool");
}

static cJSON* get_core_memory_json(void) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    BlobHeader* blob = NULL;
    cJSON* core = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CORE_MEMORY) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            core = cJSON_Parse(data);
            break;
        }
    }
    pthread_mutex_unlock(&arena_mutex);
    if (!core) core = cJSON_CreateObject();
    return core;
}

static char* get_core_memory_string(void) {
    cJSON* core = get_core_memory_json();
    char* str = cJSON_PrintUnformatted(core);
    cJSON_Delete(core);
    return str;
}

static char* build_tool_descriptions(void) {
    char* result = NULL;
    size_t len = 0;
    FILE* out = open_memstream(&result, &len);
    if (!out) return strdup("");
    fprintf(out, "You have access to the following tools:\n\n");
    for (Tool* t = tools; t->name; t++) {
        fprintf(out, "### %s\n", t->name);
        if (t->description) fprintf(out, "Description: %s\n", t->description);
        if (t->parameters) fprintf(out, "Parameters: %s\n", t->parameters);
        if (t->example) fprintf(out, "Example: %s\n", t->example);
        fprintf(out, "\n");
    }
    fclose(out);
    return result;
}

// ------------------------------------------------------------------
// LLM communication
// ------------------------------------------------------------------
static const char* ollama_endpoint = "http://localhost:11434/api/generate";
static const char* ollama_model = "tinyllama:1.1b";
static long llm_connect_timeout = 10;
static long llm_total_timeout    = 120;
static int  llm_retry_attempts   = 3;

static int progress_callback(void *clientp,
                             curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal;
    static time_t last_update = 0;
    time_t now = time(NULL);
    if (now - last_update >= 5) {
        fprintf(stderr, ".");
        fflush(stderr);
        last_update = now;
    }
    return 0;
}

static char* extract_json_block(const char* input) {
    const char* start = strchr(input, '{');
    if (!start) return NULL;
    int depth = 0;
    const char* p = start;
    while (*p) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) {
                int len = p - start + 1;
                char* result = malloc(len + 1);
                if (result) {
                    memcpy(result, start, len);
                    result[len] = '\0';
                }
                return result;
            }
        }
        p++;
    }
    return NULL;
}

static char* call_llm(const char* prompt, long total_timeout) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, COLOR_RED "Error: curl_easy_init failed\n" COLOR_RESET);
        return NULL;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", ollama_model);
    cJSON_AddStringToObject(root, "prompt", prompt);
    cJSON_AddBoolToObject(root, "stream", 0);
    cJSON_AddStringToObject(root, "format", "json");
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char* response = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, ollama_endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, llm_connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(json_str);

    if (res != CURLE_OK) {
        fprintf(stderr, COLOR_RED "\nLLM call failed: %s\n" COLOR_RESET, curl_easy_strerror(res));
        fprintf(stderr, "  Endpoint: %s\n", ollama_endpoint);
        fprintf(stderr, "  Model: %s\n", ollama_model);
        fprintf(stderr, "  Timeout: %ld seconds\n", total_timeout);
        free(response);
        return NULL;
    }

    cJSON* resp = cJSON_Parse(response);
    if (!resp) {
        fprintf(stderr, COLOR_RED "\nLLM returned invalid JSON. Attempting repair.\n" COLOR_RESET);
        char* repaired = extract_json_block(response);
        if (repaired) {
            resp = cJSON_Parse(repaired);
            free(repaired);
        }
        if (!resp) {
            fprintf(stderr, "Raw response (first 200 chars):\n%.200s\n", response ? response : "(null)");
            free(response);
            return NULL;
        }
    }

    cJSON* err = cJSON_GetObjectItem(resp, "error");
    if (err && cJSON_IsString(err)) {
        fprintf(stderr, COLOR_RED "\nOllama error: %s\n" COLOR_RESET, err->valuestring);
        cJSON_Delete(resp);
        free(response);
        return NULL;
    }

    cJSON* text = cJSON_GetObjectItem(resp, "response");
    char* result = text && cJSON_IsString(text) ? strdup(text->valuestring) : NULL;
    cJSON_Delete(resp);
    free(response);
    return result;
}

static char* call_llm_with_retry(const char* prompt) {
    int base_delay = 2;
    for (int attempt = 0; attempt < llm_retry_attempts; attempt++) {
        if (attempt > 0) {
            int delay = base_delay * (1 << (attempt - 1));
            int jitter = (rand() % (delay/5 + 1)) - (delay/10);
            delay += jitter;
            if (delay < 1) delay = 1;
            if (delay > 30) delay = 30;
            fprintf(stderr, COLOR_YELLOW "\nRetrying LLM call (attempt %d/%d) after %d seconds...\n" COLOR_RESET,
                    attempt+1, llm_retry_attempts, delay);
            fflush(stderr);
            sleep(delay);
        }
        long timeout = llm_total_timeout + (attempt * 30);
        if (timeout > 300) timeout = 300;
        char* result = call_llm(prompt, timeout);
        if (result) {
            fprintf(stderr, "\n");
            return result;
        }
    }
    fprintf(stderr, COLOR_RED "\nAll LLM retry attempts failed.\n" COLOR_RESET);
    return NULL;
}

static int check_ollama_available(void) {
    CURL* curl = curl_easy_init();
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_URL, ollama_endpoint);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

static void log_message(const char* msg) {
    if (!log_file) return;
    time_t now = time(NULL);
    struct tm tm_buf;
    char timestr[128];
    localtime_r(&now, &tm_buf);
    strftime(timestr, sizeof(timestr), "%c", &tm_buf);
    fprintf(log_file, "[%s] %s\n", timestr, msg);
    fflush(log_file);
}

// ------------------------------------------------------------------
// ReWOO prompt builders
// ------------------------------------------------------------------
static char* build_rewoo_plan_prompt(const char* user_msg, void* shadow) {
    cJSON* core = get_core_memory_json();
    char* core_str = cJSON_PrintUnformatted(core);
    cJSON_Delete(core);

    char* history = NULL;
    size_t hist_len = 0;
    FILE* hist_out = open_memstream(&history, &hist_len);
    if (hist_out) {
        BlobHeader* blobs[20];
        int count = 0;
        BlobHeader* blob = NULL;
        while ((blob = blob_iterate(shadow, blob)) != NULL && count < 20) {
            if (blob->kind == BLOB_KIND_USER || blob->kind == BLOB_KIND_ASSISTANT ||
                blob->kind == BLOB_KIND_TOOL_CALL || blob->kind == BLOB_KIND_TOOL_RESULT) {
                blobs[count++] = blob;
            }
        }
        int start = count > 10 ? count - 10 : 0;
        for (int i = start; i < count; i++) {
            blob = blobs[i];
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            const char* role = blob->kind == BLOB_KIND_USER ? "User" :
                               blob->kind == BLOB_KIND_ASSISTANT ? "Assistant" :
                               blob->kind == BLOB_KIND_TOOL_CALL ? "Tool call" : "Tool result";
            fprintf(hist_out, "%s: %s\n", role, data);
        }
        fclose(hist_out);
    }

    char* tools_desc = build_tool_descriptions();
    const char* example_json = "{\"steps\":[{\"tool\":\"http_get\",\"args\":\"https://example.com\"}]}";
    char* prompt;
    int needed = asprintf(&prompt,
        "You are Shadowclaw, an AI assistant with access to tools.\n\n"
        "Core memory (persistent facts): %s\n\n"
        "Recent conversation (last messages):\n%s\n\n"
        "Tool descriptions:\n%s\n\n"
        "Now the user says: \"%s\"\n\n"
        "You must create a plan using the tools. "
        "Output a JSON object with a single key \"steps\" that is an array of objects, each with \"tool\" and \"args\". "
        "Do not include any other text. Example: %s\n"
        "Plan:",
        core_str ? core_str : "{}",
        history ? history : "",
        tools_desc,
        user_msg,
        example_json);
    free(core_str);
    free(history);
    free(tools_desc);
    if (needed < 0) return NULL;
    return prompt;
}

static int is_valid_plan(const char* json_str) {
    if (!json_str) return 0;
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        char* extracted = extract_json_block(json_str);
        if (extracted) {
            root = cJSON_Parse(extracted);
            free(extracted);
        }
    }
    if (!root) return 0;
    cJSON* steps = cJSON_GetObjectItem(root, "steps");
    int valid = steps && cJSON_IsArray(steps);
    cJSON_Delete(root);
    return valid;
}

typedef struct {
    char* tool;
    char* args;
    char* raw_json;
} ToolStep;

static int parse_rewoo_plan(const char* json_str, ToolStep** steps_out, int* count_out) {
    char* repaired = NULL;
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        repaired = extract_json_block(json_str);
        if (repaired) root = cJSON_Parse(repaired);
    }
    if (!root) {
        free(repaired);
        return 0;
    }
    cJSON* steps_arr = cJSON_GetObjectItem(root, "steps");
    if (!cJSON_IsArray(steps_arr)) {
        cJSON_Delete(root);
        free(repaired);
        return 0;
    }
    int count = cJSON_GetArraySize(steps_arr);
    ToolStep* steps = malloc(count * sizeof(ToolStep));
    if (!steps) {
        cJSON_Delete(root);
        free(repaired);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cJSON* step = cJSON_GetArrayItem(steps_arr, i);
        cJSON* tool = cJSON_GetObjectItem(step, "tool");
        cJSON* args = cJSON_GetObjectItem(step, "args");
        if (!cJSON_IsString(tool) || !args) {
            for (int j = 0; j < i; j++) {
                free(steps[j].tool);
                free(steps[j].args);
                free(steps[j].raw_json);
            }
            free(steps);
            cJSON_Delete(root);
            free(repaired);
            return 0;
        }
        steps[i].tool = strdup(tool->valuestring);
        if (cJSON_IsString(args)) {
            steps[i].args = strdup(args->valuestring);
        } else if (cJSON_IsArray(args)) {
            int arr_len = cJSON_GetArraySize(args);
            size_t total = 0;
            for (int j = 0; j < arr_len; j++) {
                cJSON* elem = cJSON_GetArrayItem(args, j);
                if (cJSON_IsString(elem)) total += strlen(elem->valuestring) + (j > 0 ? 1 : 0);
            }
            steps[i].args = malloc(total + 1);
            if (steps[i].args) {
                steps[i].args[0] = '\0';
                for (int j = 0; j < arr_len; j++) {
                    cJSON* elem = cJSON_GetArrayItem(args, j);
                    if (cJSON_IsString(elem)) {
                        if (j > 0) strcat(steps[i].args, " ");
                        strcat(steps[i].args, elem->valuestring);
                    }
                }
            } else {
                steps[i].args = strdup("");
            }
        } else {
            steps[i].args = strdup("");
        }
        steps[i].raw_json = cJSON_PrintUnformatted(step);
    }
    cJSON_Delete(root);
    free(repaired);
    *steps_out = steps;
    *count_out = count;
    return 1;
}

static char* build_synthesis_prompt(const char* user_msg, const char* plan_json,
                                     char* observations[], int n) {
    char* plan_str = NULL;
    cJSON* root = cJSON_Parse(plan_json);
    if (root) {
        plan_str = cJSON_Print(root);
        cJSON_Delete(root);
    }
    if (!plan_str) plan_str = strdup(plan_json);

    char* obs_str = NULL;
    size_t obs_len = 0;
    FILE* obs_out = open_memstream(&obs_str, &obs_len);
    if (obs_out) {
        for (int i = 0; i < n; i++) {
            const char* ob = observations[i] ? observations[i] : "(none)";
            char truncated[501];
            strncpy(truncated, ob, 500);
            truncated[500] = '\0';
            fprintf(obs_out, "Step %d observation:\n%s\n\n", i+1, truncated);
        }
        fclose(obs_out);
    }

    char* prompt;
    int needed = asprintf(&prompt,
        "Based on the user's request and the plan that was executed, provide a final answer.\n\n"
        "User request: %s\n\n"
        "Plan executed:\n%s\n\n"
        "Observations:\n%s\n\n"
        "Final answer:",
        user_msg, plan_str, obs_str ? obs_str : "");
    free(plan_str);
    free(obs_str);
    if (needed < 0) return NULL;
    return prompt;
}

// ------------------------------------------------------------------
// Summarizer for heartbeat
// ------------------------------------------------------------------
static char* summarize_recent(int num_messages) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = shadow_global;
    BlobHeader* blobs[64];
    int count = 0;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL && count < 64) {
        if (blob->kind != BLOB_KIND_SYSTEM && blob->kind != BLOB_KIND_CORE_MEMORY) {
            blobs[count++] = blob;
        }
    }
    char* history = NULL;
    size_t hist_len = 0;
    FILE* hist_out = open_memstream(&history, &hist_len);
    if (hist_out) {
        int start = count > num_messages ? count - num_messages : 0;
        for (int i = start; i < count; i++) {
            blob = blobs[i];
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            const char* role = blob->kind == BLOB_KIND_USER ? "User" :
                               blob->kind == BLOB_KIND_ASSISTANT ? "Assistant" :
                               blob->kind == BLOB_KIND_TOOL_CALL ? "Tool call" : "Tool result";
            fprintf(hist_out, "%s: %s\n", role, data);
        }
        fclose(hist_out);
    }
    pthread_mutex_unlock(&arena_mutex);

    if (!history) return strdup("");
    char* prompt;
    asprintf(&prompt, "Summarize the following conversation excerpt concisely, capturing key facts and decisions:\n\n%s", history);
    free(history);
    char* summary = call_llm_with_retry(prompt);
    free(prompt);
    return summary ? summary : strdup("");
}

// ------------------------------------------------------------------
// Cron and webhooks (FIXED: mutex handling, schedule parsing)
// ------------------------------------------------------------------
typedef struct PendingJob {
    char tool[64];
    char args[256];
    struct PendingJob *next;
} PendingJob;

static PendingJob *job_queue = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_t cron_thread;
static volatile int cron_running = 1;

static int parse_schedule(const char* sched) {
    if (strncmp(sched, "@every ", 7) == 0) {
        const char* num_part = sched + 7;
        int val = atoi(num_part);
        const char* unit = num_part;
        while (*unit && isdigit(*unit)) unit++;
        if (strcmp(unit, "s") == 0 || *unit == '\0') return val;
        if (strcmp(unit, "m") == 0) return val * 60;
        if (strcmp(unit, "h") == 0) return val * 3600;
        return val;
    }
    if (strcmp(sched, "@hourly") == 0) return 3600;
    if (strcmp(sched, "@daily") == 0) return 86400;
    if (strcmp(sched, "@weekly") == 0) return 604800;
    return 0;
}

static const char* parse_schedule_string(const char* sched) {
    static char buf[32];
    if (strcmp(sched, "hourly") == 0) return "@hourly";
    if (strcmp(sched, "daily") == 0) return "@daily";
    if (strcmp(sched, "weekly") == 0) return "@weekly";
    char* end;
    long val = strtol(sched, &end, 10);
    if (end != sched && *end) {
        if (strcmp(end, "s") == 0) { snprintf(buf, sizeof(buf), "@every %lds", val); return buf; }
        if (strcmp(end, "m") == 0) { snprintf(buf, sizeof(buf), "@every %ldm", val); return buf; }
        if (strcmp(end, "h") == 0) { snprintf(buf, sizeof(buf), "@every %ldh", val); return buf; }
    }
    return NULL;
}

static void update_cron_last_run(void** arena_ptr, BlobHeader* old_blob, time_t last_run) {
    const char* old_data = (const char*)old_blob + BLOB_HEADER_SIZE;
    cJSON* json = cJSON_Parse(old_data);
    if (!json) return;
    cJSON_SetNumberValue(cJSON_GetObjectItem(json, "last_run"), (double)last_run);
    char* new_data = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (new_data) {
        old_blob->kind = BLOB_KIND_DELETED;
        if (!blob_append(arena_ptr, BLOB_KIND_CRONJOB, new_data)) {
            fprintf(stderr, COLOR_RED "update_cron_last_run: blob_append failed\n" COLOR_RESET);
        }
        free(new_data);
    }
}

static void run_heartbeat_summary(void** arena_ptr) {
    char* summary = summarize_recent(20);
    if (summary && *summary) {
        pthread_mutex_lock(&arena_mutex);
        blob_append(arena_ptr, BLOB_KIND_SYSTEM, summary);
        pthread_mutex_unlock(&arena_mutex);
    }
    free(summary);
}

static void* cron_loop(void* arg) {
    (void)arg;
    struct timespec ts = {5, 0};
    while (cron_running) {
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&arena_mutex);
        void* shadow = shadow_global;
        if (!shadow) {
            pthread_mutex_unlock(&arena_mutex);
            continue;
        }
        time_t now = time(NULL);
        int restart = 1;
        while (restart) {
            restart = 0;
            BlobHeader* blob = NULL;
            while ((blob = blob_iterate(shadow, blob)) != NULL) {
                if (blob->kind == BLOB_KIND_CRONJOB) {
                    const char* data = (const char*)blob + BLOB_HEADER_SIZE;
                    cJSON* json = cJSON_Parse(data);
                    if (!json) continue;
                    cJSON* sched = cJSON_GetObjectItem(json, "schedule");
                    cJSON* tool = cJSON_GetObjectItem(json, "tool");
                    cJSON* args = cJSON_GetObjectItem(json, "args");
                    cJSON* last_run_json = cJSON_GetObjectItem(json, "last_run");
                    if (cJSON_IsString(sched) && cJSON_IsString(tool) && cJSON_IsString(args) && cJSON_IsNumber(last_run_json)) {
                        if (strcmp(sched->valuestring, "@heartbeat") == 0) {
                            time_t last_run = (time_t)last_run_json->valuedouble;
                            if ((now - last_run) >= 120) {
                                pthread_mutex_unlock(&arena_mutex);
                                char* result = execute_tool("heartbeat", "");
                                free(result);
                                pthread_mutex_lock(&arena_mutex);
                                // re-fetch blob pointer (may have changed)
                                // simple: set restart flag and break to re-iterate
                                update_cron_last_run(&shadow_global, blob, now);
                                shadow = shadow_global;
                                restart = 1;
                                cJSON_Delete(json);
                                break;
                            }
                        } else {
                            int interval = parse_schedule(sched->valuestring);
                            time_t last_run = (time_t)last_run_json->valuedouble;
                            if (interval > 0 && (now - last_run) >= interval) {
                                PendingJob* job = malloc(sizeof(PendingJob));
                                if (job) {
                                    strncpy(job->tool, tool->valuestring, sizeof(job->tool)-1);
                                    job->tool[sizeof(job->tool)-1] = '\0';
                                    strncpy(job->args, args->valuestring, sizeof(job->args)-1);
                                    job->args[sizeof(job->args)-1] = '\0';
                                    job->next = NULL;
                                    pthread_mutex_lock(&queue_mutex);
                                    PendingJob** p = &job_queue;
                                    while (*p) p = &(*p)->next;
                                    *p = job;
                                    pthread_cond_signal(&queue_cond);
                                    pthread_mutex_unlock(&queue_mutex);
                                }
                                update_cron_last_run(&shadow_global, blob, now);
                                shadow = shadow_global;
                                restart = 1;
                                cJSON_Delete(json);
                                break;
                            }
                        }
                    }
                    cJSON_Delete(json);
                }
            }
        }
        pthread_mutex_unlock(&arena_mutex);
    }
    return NULL;
}

static void trigger_webhooks(void** arena_ptr, const char* event, const char* data) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = *arena_ptr;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_WEBHOOK) {
            const char* entry = (const char*)blob + BLOB_HEADER_SIZE;
            cJSON* json = cJSON_Parse(entry);
            if (!json) continue;
            cJSON* url = cJSON_GetObjectItem(json, "url");
            cJSON* ev = cJSON_GetObjectItem(json, "event");
            if (cJSON_IsString(url) && cJSON_IsString(ev) && strcmp(ev->valuestring, event) == 0) {
                cJSON* payload_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(payload_obj, "event", event);
                cJSON_AddStringToObject(payload_obj, "data", data);
                char* payload = cJSON_PrintUnformatted(payload_obj);
                cJSON_Delete(payload_obj);
                if (payload) {
                    pthread_mutex_unlock(&arena_mutex);
                    http_post(url->valuestring, payload);
                    pthread_mutex_lock(&arena_mutex);
                    free(payload);
                }
            }
            cJSON_Delete(json);
        }
    }
    pthread_mutex_unlock(&arena_mutex);
}

static void process_job_queue(void** arena_ptr) {
    pthread_mutex_lock(&queue_mutex);
    PendingJob* job = job_queue;
    job_queue = NULL;
    pthread_mutex_unlock(&queue_mutex);

    while (job) {
        PendingJob* next = job->next;
        printf(COLOR_CYAN "\n[Cron] Running %s with args: %s\n" COLOR_RESET, job->tool, job->args);
        char* result = execute_tool(job->tool, job->args);
        printf(COLOR_CYAN "[Cron result] %s\n" COLOR_RESET, result);

        char msg[512];
        snprintf(msg, sizeof(msg), "[Cron @ %s] %s → %s", job->tool, job->args, result);
        pthread_mutex_lock(&arena_mutex);
        blob_append(arena_ptr, BLOB_KIND_SYSTEM, msg);
        blob_append(arena_ptr, BLOB_KIND_TOOL_CALL, "cron job");
        blob_append(arena_ptr, BLOB_KIND_TOOL_RESULT, result);
        pthread_mutex_unlock(&arena_mutex);

        char event[128];
        snprintf(event, sizeof(event), "cron:%s", job->tool);
        trigger_webhooks(arena_ptr, event, result);

        free(result);
        free(job);
        job = next;
    }
}

// ------------------------------------------------------------------
// Skill execution
// ------------------------------------------------------------------
static char* execute_skill(const char* skill_name, const char* args_str, void** arena_ptr) {
    pthread_mutex_lock(&arena_mutex);
    void* shadow = *arena_ptr;
    BlobHeader* blob = NULL;
    cJSON* steps = NULL;
    char* interpreter_cmd = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_SKILL) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            cJSON* json = cJSON_Parse(data);
            if (json) {
                cJSON* name = cJSON_GetObjectItem(json, "name");
                if (cJSON_IsString(name) && strcmp(name->valuestring, skill_name) == 0) {
                    steps = cJSON_Duplicate(cJSON_GetObjectItem(json, "steps"), 1);
                    cJSON* ic = cJSON_GetObjectItem(json, "interpreter_command");
                    if (cJSON_IsString(ic)) interpreter_cmd = strdup(ic->valuestring);
                    cJSON_Delete(json);
                    break;
                }
                cJSON_Delete(json);
            }
        }
    }
    pthread_mutex_unlock(&arena_mutex);

    if (interpreter_cmd) {
        char* full_cmd;
        asprintf(&full_cmd, "%s %s", interpreter_cmd, args_str);
        char* result = interpret_command(full_cmd);
        free(full_cmd);
        free(interpreter_cmd);
        cJSON_Delete(steps);
        return result;
    }

    if (!steps || !cJSON_IsArray(steps)) {
        if (steps) cJSON_Delete(steps);
        return strdup("Skill not found or has no steps");
    }

    char* args_copy = strdup(args_str);
    if (!args_copy) { cJSON_Delete(steps); return strdup("Memory error"); }
    char* argv[16];
    int argc = 0;
    char *saveptr;
    char* token = strtok_r(args_copy, " ", &saveptr);
    while (token && argc < 16) argv[argc++] = token, token = strtok_r(NULL, " ", &saveptr);

    char* last_result = NULL;
    cJSON* step;
    cJSON_ArrayForEach(step, steps) {
        if (!cJSON_IsObject(step)) continue;
        cJSON* step_tool = cJSON_GetObjectItem(step, "tool");
        cJSON* step_args = cJSON_GetObjectItem(step, "args");
        if (!cJSON_IsString(step_tool) || !cJSON_IsString(step_args)) continue;

        const char* tmpl = step_args->valuestring;
        char* substituted = NULL;
        size_t sub_len = 0;
        FILE* sub_stream = open_memstream(&substituted, &sub_len);
        if (!sub_stream) { free(last_result); free(args_copy); cJSON_Delete(steps); return strdup("Memory error"); }

        while (*tmpl) {
            if (*tmpl == '{') {
                const char* close = strchr(tmpl, '}');
                if (!close) break;
                char placeholder[16];
                int len = close - tmpl - 1;
                if (len > 0 && len < 16) {
                    strncpy(placeholder, tmpl + 1, len);
                    placeholder[len] = '\0';
                    if (strcmp(placeholder, "args") == 0) fputs(args_str, sub_stream);
                    else if (placeholder[0] >= '0' && placeholder[0] <= '9') {
                        int idx = atoi(placeholder);
                        if (idx >= 0 && idx < argc) fputs(argv[idx], sub_stream);
                    } else if (strcmp(placeholder, "result") == 0 && last_result) fputs(last_result, sub_stream);
                }
                tmpl = close + 1;
            } else { fputc(*tmpl, sub_stream); tmpl++; }
        }
        fclose(sub_stream);
        char* step_result = execute_tool(step_tool->valuestring, substituted);
        free(substituted);
        free(last_result);
        last_result = step_result;
    }

    free(args_copy);
    cJSON_Delete(steps);
    return last_result ? last_result : strdup("");
}

// ------------------------------------------------------------------
// Help system (enhanced with colors)
// ------------------------------------------------------------------
static void print_help(void* shadow) {
    pthread_mutex_lock(&arena_mutex);
    printf(COLOR_BOLD "\nShadowclaw v%s commands:\n" COLOR_RESET, SHADOWCLAW_VERSION);
    printf("  %-12s %s\n", "/help", "Show this help");
    printf("  %-12s %s\n", "/tools", "List available tools");
    printf("  %-12s %s\n", "/state", "Show arena memory stats and soul file info");
    printf("  %-12s %s\n", "/clear", "Clear conversation history");
    printf("  %-12s %s\n", "/chat", "Remind you that chat mode is active");
    printf("  %-12s %s\n", "/exit", "Exit Shadowclaw");
    printf("  %-12s %s\n", "/loop", "Schedule a recurring task: /loop <schedule> <tool> [args...]");
    printf("  %-12s %s\n", "/crons", "List scheduled cron jobs");
    printf("  %-12s %s\n", "/webhooks", "List registered webhooks");
    printf("  %-12s %s\n", "/skills", "List dynamic skills");
    printf("  %-12s %s\n", "/compact", "Manually compact arena");
    printf("  %-12s %s\n", "/soul", "Show path to shadowsoul.md and last update");

    printf(COLOR_BOLD "\nBuilt-in tools:\n" COLOR_RESET);
    for (Tool *t = tools; t->name; t++) printf("  %s\n", t->name);

    int skill_count = 0;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_SKILL) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            cJSON* json = cJSON_Parse(data);
            if (json) {
                cJSON* name = cJSON_GetObjectItem(json, "name");
                cJSON* desc = cJSON_GetObjectItem(json, "desc");
                if (cJSON_IsString(name) && cJSON_IsString(desc)) {
                    if (skill_count == 0) printf(COLOR_BOLD "\nDynamic skills:\n" COLOR_RESET);
                    printf("  %-12s %s\n", name->valuestring, desc->valuestring);
                    skill_count++;
                }
                cJSON_Delete(json);
            }
        }
    }
    if (skill_count == 0) printf("\nNo dynamic skills loaded. Use skill_add to create one.\n");

    struct stat st;
    if (stat(soul_file, &st) == 0) {
        char timebuf[128];
        struct tm tm_buf;
        localtime_r(&st.st_mtime, &tm_buf);
        strftime(timebuf, sizeof(timebuf), "%c", &tm_buf);
        printf("\nSoul file: %s (%ld bytes, last modified: %s", soul_file, st.st_size, timebuf);
    } else {
        printf("\nSoul file not yet created.\n");
    }
    printf("\n");
    pthread_mutex_unlock(&arena_mutex);
}

// ------------------------------------------------------------------
// Signal handler for clean shutdown
// ------------------------------------------------------------------
static void handle_sigint(int sig) {
    (void)sig;
    cron_running = 0;
    pthread_cond_signal(&queue_cond);
}

// ------------------------------------------------------------------
// Asynchronous output queue (to avoid interrupting input)
// ------------------------------------------------------------------
static char* pending_log = NULL;

void queue_log(const char* msg) {
    if (!msg) return;
    size_t new_len = (pending_log ? strlen(pending_log) : 0) + strlen(msg) + 2;
    char* new_log = realloc(pending_log, new_len);
    if (!new_log) return;
    pending_log = new_log;
    if (pending_log[0]) strcat(pending_log, "\n");
    strcat(pending_log, msg);
}

static void flush_pending_log(void) {
    if (pending_log) {
        printf("%s\n", pending_log);
        fflush(stdout);
        free(pending_log);
        pending_log = NULL;
    }
}

// ------------------------------------------------------------------
// Main
// ------------------------------------------------------------------
int main(int argc, char** argv) {
    int no_llm_mode = 0;
    srand(time(NULL));

    char* env;
    if ((env = getenv("SHADOWCLAW_CONNECT_TIMEOUT")) != NULL) { long val = atol(env); if (val > 0) llm_connect_timeout = val; }
    if ((env = getenv("SHADOWCLAW_TOTAL_TIMEOUT")) != NULL) { long val = atol(env); if (val > 0) llm_total_timeout = val; }
    if ((env = getenv("SHADOWCLAW_RETRY_ATTEMPTS")) != NULL) { int val = atoi(env); if (val > 0 && val <= 10) llm_retry_attempts = val; }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-llm") == 0) no_llm_mode = 1;
        else if (strcmp(argv[i], "--dry-run") == 0) dry_run = 1;
        else if (strcmp(argv[i], "--log") == 0 && i+1 < argc) {
            log_file = fopen(argv[++i], "a");
            if (!log_file) fprintf(stderr, COLOR_RED "Failed to open log file\n" COLOR_RESET);
        } else if (strcmp(argv[i], "-f") == 0 && i+1 < argc) {
            strncpy(state_filename, argv[++i], sizeof(state_filename)-1);
            state_filename[sizeof(state_filename)-1] = '\0';
        }
    }

    CURLcode curl_init = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_init != CURLE_OK) {
        fprintf(stderr, COLOR_RED "Failed to initialize libcurl: %s\n" COLOR_RESET, curl_easy_strerror(curl_init));
        return 1;
    }

    ensure_soul_directory();

    shadow_global = load_state(state_filename);
    if (!shadow_global) {
        shadow_global = arena_grow(NULL, 512 * 1024);
        if (!shadow_global) {
            fprintf(stderr, COLOR_RED "Failed to allocate arena\n" COLOR_RESET);
            curl_global_cleanup();
            return 1;
        }
        const char* system_prompt = "You are Shadowclaw, a helpful AI assistant with access to tools. When you need to use a tool, you must respond with a JSON plan. Core memory will be provided at the beginning of each prompt.";
        if (!blob_append(&shadow_global, BLOB_KIND_SYSTEM, system_prompt)) fprintf(stderr, COLOR_RED "Failed to append system prompt\n" COLOR_RESET);
        if (!blob_append(&shadow_global, BLOB_KIND_CORE_MEMORY, "{}")) fprintf(stderr, COLOR_RED "Failed to create core memory\n" COLOR_RESET);
        save_state(&shadow_global, state_filename);
    }

    pthread_mutex_lock(&arena_mutex);
    int heartbeat_exists = 0;
    BlobHeader* blob = NULL;
    while ((blob = blob_iterate(shadow_global, blob)) != NULL) {
        if (blob->kind == BLOB_KIND_CRONJOB) {
            const char* data = (const char*)blob + BLOB_HEADER_SIZE;
            if (strstr(data, "\"tool\":\"heartbeat\"")) { heartbeat_exists = 1; break; }
        }
    }
    if (!heartbeat_exists) {
        char* heartbeat_json = build_json_object("schedule", "@every 120s", "tool", "heartbeat", "args", "", "last_run", "0");
        if (heartbeat_json) {
            blob_append(&shadow_global, BLOB_KIND_CRONJOB, heartbeat_json);
            free(heartbeat_json);
        }
    }
    pthread_mutex_unlock(&arena_mutex);

    sync_soul_file();

    if (!no_llm_mode && !check_ollama_available()) {
        fprintf(stderr, COLOR_YELLOW "\nOllama not available at %s. Enabling --no-llm mode.\n" COLOR_RESET, ollama_endpoint);
        no_llm_mode = 1;
    }

    if (!no_llm_mode) {
        printf(COLOR_GREEN "\nConnected to Ollama at %s (model: %s). Timeouts: connect=%ld, total=%ld, retries=%d\n" COLOR_RESET,
               ollama_endpoint, ollama_model, llm_connect_timeout, llm_total_timeout, llm_retry_attempts);
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (pthread_create(&cron_thread, NULL, cron_loop, NULL) != 0) {
        fprintf(stderr, COLOR_RED "Failed to create cron thread\n" COLOR_RESET);
    }

    printf(COLOR_BOLD "\n========================================\n" COLOR_RESET);
    printf(COLOR_BOLD "  Shadowclaw v%s – Self-contained AI agent\n" COLOR_RESET, SHADOWCLAW_VERSION);
    printf(COLOR_BOLD "  with unified soul memory at %s\n" COLOR_RESET, soul_file);
    if (dry_run) printf(COLOR_YELLOW "  *** DRY RUN MODE – tools will not execute ***\n" COLOR_RESET);
    if (no_llm_mode) printf(COLOR_YELLOW "  *** NO-LLM MODE – using local interpreter only ***\n" COLOR_RESET);
    printf(COLOR_BOLD "========================================\n" COLOR_RESET);
    print_help(shadow_global);

#ifdef USE_READLINE
    rl_attempted_completion_function = NULL;
    using_history();
#endif

    while (1) {
        process_job_queue(&shadow_global);
        flush_pending_log();

#ifdef USE_READLINE
        char* buf = readline(COLOR_GREEN "> " COLOR_RESET);
        if (!buf) break;
        if (*buf) add_history(buf);
#else
        printf(COLOR_GREEN "> " COLOR_RESET);
        fflush(stdout);
        char buf_static[4096];
        if (!fgets(buf_static, sizeof(buf_static), stdin)) break;
        buf_static[strcspn(buf_static, "\n")] = 0;
        char* buf = buf_static;
#endif

        if (buf[0] == '/') {
            if (strcmp(buf, "/help") == 0) print_help(shadow_global);
            else if (strcmp(buf, "/tools") == 0) {
                pthread_mutex_lock(&arena_mutex);
                printf(COLOR_BOLD "Built-in tools:\n" COLOR_RESET);
                for (Tool *t = tools; t->name; t++) printf("  %s\n", t->name);
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/state") == 0) {
                pthread_mutex_lock(&arena_mutex);
                ShadowHeader* hdr = (ShadowHeader*)((char*)shadow_global - HEADER_SIZE);
                printf("Arena capacity: %zu bytes\n", hdr->capacity);
                printf("Arena used: %zu bytes\n", hdr->used);
                struct stat st;
                if (stat(soul_file, &st) == 0) {
                    char timebuf[128];
                    struct tm tm_buf;
                    localtime_r(&st.st_mtime, &tm_buf);
                    strftime(timebuf, sizeof(timebuf), "%c", &tm_buf);
                    printf("Soul file: %s (%ld bytes, last modified: %s", soul_file, st.st_size, timebuf);
                }
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/clear") == 0) {
                pthread_mutex_lock(&arena_mutex);
                ShadowHeader* hdr = (ShadowHeader*)((char*)shadow_global - HEADER_SIZE);
                hdr->used = 0;
                const char* system_prompt = "You are Shadowclaw, a helpful AI assistant with access to tools. When you need to use a tool, you must respond with a JSON plan. Core memory will be provided at the beginning of each prompt.";
                blob_append(&shadow_global, BLOB_KIND_SYSTEM, system_prompt);
                blob_append(&shadow_global, BLOB_KIND_CORE_MEMORY, "{}");
                pthread_mutex_unlock(&arena_mutex);
                sync_soul_file();
                printf("Conversation cleared and soul file updated.\n");
            } else if (strcmp(buf, "/exit") == 0) break;
            else if (strncmp(buf, "/loop", 5) == 0) {
                char* p = buf + 5;
                while (*p == ' ') p++;
                if (*p == '\0') {
                    printf("Usage: /loop <schedule> <tool> [args...]\nExamples:\n  /loop 1h http_get https://example.com/news\n  /loop daily file_read ~/notes.txt\n");
                    continue;
                }
                char* sched_start = p;
                while (*p && !isspace(*p)) p++;
                if (!*p) { printf("Missing tool after schedule.\n"); continue; }
                *p++ = '\0';
                const char* sched_str = parse_schedule_string(sched_start);
                if (!sched_str) { printf("Unrecognized schedule format. Use e.g., 1h, 30m, daily, hourly, weekly.\n"); continue; }
                while (*p == ' ') p++;
                if (*p == '\0') { printf("Missing tool name.\n"); continue; }
                char* tool_start = p;
                while (*p && !isspace(*p)) p++;
                if (*p) *p++ = '\0';
                while (*p == ' ') p++;
                char* args = p;
                char* json = build_json_object("schedule", sched_str, "tool", tool_start, "args", args, "last_run", "0");
                if (!json) { printf("Failed to create cron job JSON.\n"); continue; }
                pthread_mutex_lock(&arena_mutex);
                void* hdr = blob_append(&shadow_global, BLOB_KIND_CRONJOB, json);
                pthread_mutex_unlock(&arena_mutex);
                free(json);
                if (hdr) { printf("Scheduled: %s every %s\n", tool_start, sched_str); sync_soul_file(); }
                else printf("Failed to schedule job.\n");
            } else if (strcmp(buf, "/crons") == 0) {
                pthread_mutex_lock(&arena_mutex);
                BlobHeader* b = NULL;
                printf("Scheduled cron jobs:\n");
                int count = 0;
                while ((b = blob_iterate(shadow_global, b)) != NULL) {
                    if (b->kind == BLOB_KIND_CRONJOB) { printf("  %s\n", (const char*)b + BLOB_HEADER_SIZE); count++; }
                }
                if (count == 0) printf("  None\n");
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/webhooks") == 0) {
                pthread_mutex_lock(&arena_mutex);
                BlobHeader* b = NULL;
                printf("Registered webhooks:\n");
                int count = 0;
                while ((b = blob_iterate(shadow_global, b)) != NULL) {
                    if (b->kind == BLOB_KIND_WEBHOOK) { printf("  %s\n", (const char*)b + BLOB_HEADER_SIZE); count++; }
                }
                if (count == 0) printf("  None\n");
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/skills") == 0) {
                pthread_mutex_lock(&arena_mutex);
                BlobHeader* b = NULL;
                printf("Dynamic skills:\n");
                int count = 0;
                while ((b = blob_iterate(shadow_global, b)) != NULL) {
                    if (b->kind == BLOB_KIND_SKILL) {
                        const char* data = (const char*)b + BLOB_HEADER_SIZE;
                        cJSON* j = cJSON_Parse(data);
                        if (j) {
                            cJSON* n = cJSON_GetObjectItem(j, "name");
                            cJSON* d = cJSON_GetObjectItem(j, "desc");
                            if (cJSON_IsString(n) && cJSON_IsString(d)) { printf("  %s: %s\n", n->valuestring, d->valuestring); count++; }
                            cJSON_Delete(j);
                        }
                    }
                }
                if (count == 0) printf("  None\n");
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/compact") == 0) {
                pthread_mutex_lock(&arena_mutex);
                void* new_arena = compact_arena(shadow_global);
                if (new_arena) { shadow_global = new_arena; sync_soul_file(); printf("Arena compacted and soul file updated.\n"); }
                else printf("Compaction failed.\n");
                pthread_mutex_unlock(&arena_mutex);
            } else if (strcmp(buf, "/soul") == 0) {
                struct stat st;
                if (stat(soul_file, &st) == 0) {
                    char timebuf[128];
                    struct tm tm_buf;
                    localtime_r(&st.st_mtime, &tm_buf);
                    strftime(timebuf, sizeof(timebuf), "%c", &tm_buf);
                    printf("Soul file: %s (%ld bytes, last modified: %s", soul_file, st.st_size, timebuf);
                } else printf("Soul file not found.\n");
            } else {
                printf("Unknown command. Try /help\n");
            }
#ifdef USE_READLINE
            free(buf);
#endif
            continue;
        }

        if (no_llm_mode) {
            char* response = interpret_command(buf);
            printf(COLOR_BLUE "%s\n" COLOR_RESET, response);
            free(response);
#ifdef USE_READLINE
            free(buf);
#endif
            continue;
        }

        log_message(buf);
        pthread_mutex_lock(&arena_mutex);
        blob_append(&shadow_global, BLOB_KIND_USER, buf);
        pthread_mutex_unlock(&arena_mutex);
        sync_soul_file();

        char* plan_prompt = build_rewoo_plan_prompt(buf, shadow_global);
        if (!plan_prompt) { fprintf(stderr, COLOR_RED "Failed to build plan prompt\n" COLOR_RESET); continue; }
        log_message(plan_prompt);
        char* plan_json = call_llm_with_retry(plan_prompt);
        free(plan_prompt);
        log_message(plan_json ? plan_json : "(null)");

        if (plan_json && is_valid_plan(plan_json)) {
            ToolStep* steps = NULL;
            int n = 0;
            if (parse_rewoo_plan(plan_json, &steps, &n)) {
                int valid_plan = 1;
                for (int i = 0; i < n; i++) {
                    if (!validate_tool_call(steps[i].tool, steps[i].args)) {
                        fprintf(stderr, COLOR_RED "Rejected tool call: %s\n" COLOR_RESET, steps[i].tool);
                        valid_plan = 0; break;
                    }
                }
                if (!valid_plan) {
                    printf("Assistant: I cannot use one of the requested tools.\n");
                    free(steps); free(plan_json); continue;
                }

                char** observations = calloc(n, sizeof(char*));
                for (int i = 0; i < n; i++) {
                    observations[i] = dry_run ? strdup("[DRY RUN] skipped") : execute_tool(steps[i].tool, steps[i].args);
                    pthread_mutex_lock(&arena_mutex);
                    blob_append(&shadow_global, BLOB_KIND_TOOL_CALL, steps[i].raw_json);
                    blob_append(&shadow_global, BLOB_KIND_TOOL_RESULT, observations[i]);
                    pthread_mutex_unlock(&arena_mutex);
                    char event[128];
                    snprintf(event, sizeof(event), "tool:%s", steps[i].tool);
                    trigger_webhooks(&shadow_global, event, observations[i]);
                    printf(COLOR_YELLOW "\n[Tool %s] → %s\n" COLOR_RESET, steps[i].tool, observations[i]);
                    free(steps[i].tool); free(steps[i].args); free(steps[i].raw_json);
                }
                free(steps);
                sync_soul_file();

                char* synth_prompt = build_synthesis_prompt(buf, plan_json, observations, n);
                log_message(synth_prompt);
                char* final = call_llm_with_retry(synth_prompt);
                free(synth_prompt);
                log_message(final ? final : "(null)");

                printf(COLOR_BLUE "Assistant: %s\n" COLOR_RESET, final ? final : "[no response]");
                if (final) {
                    pthread_mutex_lock(&arena_mutex);
                    blob_append(&shadow_global, BLOB_KIND_ASSISTANT, final);
                    pthread_mutex_unlock(&arena_mutex);
                    sync_soul_file();
                    free(final);
                }
                for (int i = 0; i < n; i++) free(observations[i]);
                free(observations);
            } else {
                printf("Assistant: Invalid plan format.\n");
            }
        } else {
            if (plan_json) {
                printf(COLOR_BLUE "Assistant: %s\n" COLOR_RESET, plan_json);
                pthread_mutex_lock(&arena_mutex);
                blob_append(&shadow_global, BLOB_KIND_ASSISTANT, plan_json);
                pthread_mutex_unlock(&arena_mutex);
                sync_soul_file();
            } else {
                printf("Assistant: I'm having trouble planning. Please try again.\n");
            }
        }
        free(plan_json);

        pthread_mutex_lock(&arena_mutex);
        save_state(&shadow_global, state_filename);
        pthread_mutex_unlock(&arena_mutex);
#ifdef USE_READLINE
        free(buf);
#endif
    }

    cron_running = 0;
    pthread_join(cron_thread, NULL);
    save_state(&shadow_global, state_filename);
    if (log_file) fclose(log_file);
    curl_global_cleanup();
    return 0;
}

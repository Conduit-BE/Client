#ifndef CONDUIT_API_H
#define CONDUIT_API_H

#include <stdint.h>

#if defined(CONDUIT_BUILDING_DLL)
#define CONDUIT_API __declspec(dllexport)
#else
#define CONDUIT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CONDUIT_API_VERSION 1u
#define CONDUIT_HOTKEY_DEFAULT 0u
#define CONDUIT_HOTKEY_DISABLED UINT32_MAX

typedef struct ConduitClient ConduitClient;

typedef enum ConduitStatus {
    CONDUIT_OK = 0,
    CONDUIT_INVALID_ARGUMENT = 1,
    CONDUIT_VERSION_MISMATCH = 2,
    CONDUIT_ALREADY_RUNNING = 3,
    CONDUIT_NOT_RUNNING = 4,
    CONDUIT_INTERNAL_ERROR = 5
} ConduitStatus;

typedef struct ConduitConfig {
    uint32_t size;                 /* Must be sizeof(ConduitConfig). */
    uint32_t api_version;          /* CONDUIT_API_VERSION. */
    const char* watch_directory_utf8; /* Existing directory; copied by Create. */
    const char* log_file_utf8;      /* Append-only log path; copied by Create. */
    uint32_t hotkey_virtual_key;   /* 0 = Ctrl+R, UINT32_MAX = disabled, otherwise Ctrl+the Windows VK 1..254. */
    uint32_t poll_interval_ms;     /* 0 = 50; otherwise 10..60000. */
    uint32_t debounce_ms;          /* 0 = 150; otherwise 1..60000. */
} ConduitConfig;

typedef struct ConduitStats {
    uint32_t size;                 /* Initialize to sizeof(ConduitStats). */
    uint32_t running;
    uint64_t reload_attempts;      /* Coalesced batches, including unsupported attempts. */
    uint64_t reload_successes;
    uint64_t reload_unsupported;
    uint64_t file_changes;
    uint64_t monitor_errors;
    uint32_t logging_failed;
    uint32_t worker_failed;        /* Stop, diagnose the log, then restart. */
} ConduitStats;

/* Never call these functions from DllMain or while holding the loader lock.
 * Calls on a live context are serialized where required. Destroy and FreeLibrary
 * require exclusive ownership: finish all callers first. Destroy every context
 * before FreeLibrary. No pointers allocated by this DLL are freed by the caller.
 * RequestReload only queues work; CONDUIT_OK does not mean Minecraft was reloaded.
 */
CONDUIT_API uint32_t ConduitGetApiVersion(void);
CONDUIT_API ConduitStatus ConduitCreate(const ConduitConfig* config, ConduitClient** output);
CONDUIT_API ConduitStatus ConduitStart(ConduitClient* client);
CONDUIT_API ConduitStatus ConduitRequestReload(ConduitClient* client);
CONDUIT_API ConduitStatus ConduitGetStats(ConduitClient* client, ConduitStats* stats);
CONDUIT_API ConduitStatus ConduitStop(ConduitClient* client);
CONDUIT_API void ConduitDestroy(ConduitClient* client);
/* Stops the optional Minecraft process auto-start worker before explicit unload.
 * Call outside DllMain, after dismissing any notification. Safe if inactive. */
CONDUIT_API void ConduitAutoStop(void);

#ifdef __cplusplus
}
#endif
#endif

#define _POSIX_C_SOURCE 200809L

#include "library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>

/* Third-party dependencies */
#include <libwebsockets.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cjson/cJSON.h>

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

#define OBSWS_VERSION "1.0.0"                   /* Library version string */
#define OBSWS_PROTOCOL_VERSION 1                /* OBS WebSocket protocol version (v5 uses RPC version 1) */

/* Buffer sizing: 64KB is large enough for most OBS messages. Larger messages
   (like scene lists with many scenes) might need bigger buffers, but this is
   a reasonable default. We could make it dynamic, but that adds complexity.
   The protocol itself doesn't define a max message size, so we have to choose. */
#define OBSWS_DEFAULT_BUFFER_SIZE 65536         /* 64KB buffer for WebSocket messages */

/* Pending requests tracking: We use a linked list to track requests waiting for
   responses. 256 is a reasonable limit - you can have up to 256 requests in-flight
   at once. In practice, most apps will have way fewer. We chose a limit to prevent
   unbounded memory growth if something goes wrong and requests never complete. */
#define OBSWS_MAX_PENDING_REQUESTS 256

/* UUIDs are exactly 36 characters (8-4-4-4-12 hex digits with dashes) plus null terminator.
   We use these to match requests with their responses in the asynchronous protocol. */
#define OBSWS_UUID_LENGTH 37

/* OBS WebSocket v5 OpCodes - message type identifiers in the protocol.
   
   The OBS WebSocket v5 protocol uses opcodes to identify message types. The protocol
   is based on a request-response model layered on top of WebSocket. Here's the flow:
   
   1. Server sends HELLO (opcode 0) with auth challenge and salt
   2. Client sends IDENTIFY (opcode 1) with auth response and client info
   3. Server sends IDENTIFIED (opcode 2) if auth succeeded
   4. Client can send REQUEST messages (opcode 6)
   5. Server responds with REQUEST_RESPONSE (opcode 7)
   6. Server sends EVENT messages (opcode 5) for things happening in OBS
   
   Batch operations (opcodes 8-9) let you send multiple requests in one message,
   but we don't use them in this library - each request is sent individually.
   REIDENTIFY (opcode 3) is used if we lose connection and reconnect.
*/

#define OBSWS_OPCODE_HELLO 0                    /* Server: Initial greeting with auth info */
#define OBSWS_OPCODE_IDENTIFY 1                 /* Client: Authentication and protocol agreement */
#define OBSWS_OPCODE_IDENTIFIED 2               /* Server: Auth successful, ready for commands */
#define OBSWS_OPCODE_REIDENTIFY 3               /* Client: Re-authenticate after reconnect */
#define OBSWS_OPCODE_EVENT 5                    /* Server: Something happened in OBS */
#define OBSWS_OPCODE_REQUEST 6                  /* Client: Execute an operation in OBS */
#define OBSWS_OPCODE_REQUEST_RESPONSE 7         /* Server: Result of a client request */
#define OBSWS_OPCODE_REQUEST_BATCH 8            /* Client: Multiple requests at once (unused) */
#define OBSWS_OPCODE_REQUEST_BATCH_RESPONSE 9   /* Server: Responses to batch (unused) */

/* Event subscription flags - bitmask for which OBS event categories we subscribe to.
   
   The OBS WebSocket protocol lets you specify which events you want to receive. This
   avoids bandwidth waste - if you don't care about media playback events, don't subscribe.
   We subscribe to most categories by default (using a bitmask), but you could modify
   this to be more selective if needed.
   
   We chose a bitmask (0x7FF for all) rather than subscribing/unsubscribing individually
   because it's more efficient - one subscription message at connect-time instead of
   many individual subscribe/unsubscribe messages.
*/

#define OBSWS_EVENT_GENERAL (1 << 0)        /* General OBS events (startup, shutdown) */
#define OBSWS_EVENT_CONFIG (1 << 1)         /* Configuration change events */
#define OBSWS_EVENT_SCENES (1 << 2)         /* Scene-related events (scene switched, etc) */
#define OBSWS_EVENT_INPUTS (1 << 3)         /* Input source events (muted, volume changed) */
#define OBSWS_EVENT_TRANSITIONS (1 << 4)    /* Transition events (transition started) */
#define OBSWS_EVENT_FILTERS (1 << 5)        /* Filter events (filter added, removed) */
#define OBSWS_EVENT_OUTPUTS (1 << 6)        /* Output events (recording started, streaming stopped) */
#define OBSWS_EVENT_SCENE_ITEMS (1 << 7)    /* Scene item events (source added to scene) */
#define OBSWS_EVENT_MEDIA_INPUTS (1 << 8)   /* Media playback events (media finished) */
#define OBSWS_EVENT_VENDORS (1 << 9)        /* Vendor-specific extensions */
#define OBSWS_EVENT_UI (1 << 10)            /* UI events (Studio Mode toggled) */
#define OBSWS_EVENT_ALL 0x7FF               /* Subscribe to all event types */

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* Pending request tracking - manages asynchronous request/response pairs.
   
   The OBS WebSocket protocol is asynchronous - when you send a request, you don't
   wait for the response before continuing. Instead, responses come back later with
   a request ID matching them to the original request.
   
   This struct tracks one in-flight request. We keep a linked list of these, one for
   each request waiting for a response. When a response arrives, we find the matching
   pending_request by ID, populate the response field, and set completed=true. The
   thread that sent the request is waiting on the condition variable, so it wakes up
   and gets the response.
   
   Why use a condition variable instead of polling? Because polling wastes CPU. A
   thread waiting on a condition variable goes to sleep until the response arrives,
   at which point it's woken up. Much more efficient.
   
   Why use a timestamp? For timeout detection. If a response never arrives (OBS crashed,
   network died, etc.), we detect it by checking if the request is older than the timeout.
*/

typedef struct pending_request {
    char request_id[OBSWS_UUID_LENGTH];     /* Unique ID matching request to response */
    obsws_response_t *response;             /* Response data populated when received */
    bool completed;                         /* Flag indicating response received */
    pthread_mutex_t mutex;                  /* Protects the response/completed fields */
    pthread_cond_t cond;                    /* Waiting thread sleeps here until response arrives */
    time_t timestamp;                       /* When request was created - used for timeout detection */
    struct pending_request *next;           /* Linked list pointer to next pending request */
} pending_request_t;

/* Main connection structure - holds all state for an OBS WebSocket connection.
   
   This is the main opaque type that users interact with. It holds everything needed
   to manage one connection to OBS:
   - Configuration (where to connect, timeouts, callbacks)
   - WebSocket instance (from libwebsockets)
   - Threading state (the event thread runs in the background)
   - Buffers for sending/receiving messages
   - Pending request tracking (for async request/response)
   - Statistics (for monitoring)
   - Authentication state (challenge/salt for password auth)
   
   Why is it opaque (hidden in the .c file)? So we can change the internal structure
   without breaking the API. Callers just use the pointer, they don't know what's inside.
   
   Threading model: Each connection has one background thread (event_thread) that
   processes WebSocket events, calls callbacks, etc. The main application thread sends
   requests and gets responses. This avoids the app freezing while waiting for responses.
   
   Synchronization: We use many mutexes because different parts of the connection
   are accessed from different threads:
   - state_mutex protects the connection state (so both threads see consistent state)
   - send_mutex protects sending (prevents two threads from sending at the same time)
   - requests_mutex protects the pending requests list
   - stats_mutex protects the statistics counters
   - scene_mutex protects the cached current scene name
   
   The scene cache is an optimization - some operations need to know the current
   scene. Instead of querying OBS every time, we cache it and update when we get
   SceneChanged events.
*/

struct obsws_connection {
    /* === Configuration and Setup === */
    obsws_config_t config;                  /* User-provided config (copied at construction) */
    
    /* === Connection State === */
    obsws_state_t state;                    /* Current state (CONNECTED, CONNECTING, etc) */
    pthread_mutex_t state_mutex;            /* Protects state from concurrent access */
    
    /* === WebSocket Layer === */
    struct lws_context *lws_context;        /* libwebsockets context (manages the WebSocket) */
    struct lws *wsi;                        /* WebSocket instance - the actual connection */
    
    /* === Message Buffers ===
       We keep persistent buffers instead of allocating for every message because
       it's more efficient and avoids memory fragmentation. */
    char *recv_buffer;                      /* Buffer for incoming messages from OBS */
    size_t recv_buffer_size;                /* Total capacity of receive buffer */
    size_t recv_buffer_used;                /* How many bytes are currently in the buffer */
    
    char *send_buffer;                      /* Buffer for outgoing messages to OBS */
    size_t send_buffer_size;                /* Total capacity of send buffer */
    
    /* === Background Thread ===
       The event thread continuously processes WebSocket events. This allows the
       connection to receive messages and call callbacks without blocking the app. */
    pthread_t event_thread;                 /* ID of the background thread */
    pthread_mutex_t send_mutex;             /* Prevents two threads from sending simultaneously */
    bool thread_running;                    /* Is the thread currently running? */
    bool should_exit;                       /* Signal to thread: time to stop */
    
    /* === Async Request/Response Handling ===
       When you send a request, it returns immediately with a request ID. When the
       response comes back, we find the pending_request by ID and notify the waiter. */
    pending_request_t *pending_requests;    /* Linked list of in-flight requests */
    pthread_mutex_t requests_mutex;         /* Protects the linked list */
    
    /* === Performance Monitoring === */
    obsws_stats_t stats;                    /* Message counts, errors, latency, etc */
    pthread_mutex_t stats_mutex;            /* Protects stats from concurrent access */
    
    /* === Keep-Alive / Health Monitoring ===
       We send periodic pings to detect when the connection dies. If we don't get
       a pong back within the timeout, we know something is wrong. */
    time_t last_ping_sent;                  /* When we last sent a ping */
    time_t last_pong_received;              /* When we last got a pong back */
    
    /* === Reconnection ===
       If the connection drops and auto_reconnect is enabled, we try to reconnect.
       We use exponential backoff - each attempt waits longer, up to a maximum. */
    uint32_t reconnect_attempts;            /* How many times have we tried reconnecting */
    uint32_t current_reconnect_delay;       /* How long we're waiting before next attempt */
    
    /* === Authentication State ===
       OBS uses a challenge-response authentication scheme. The server sends a
       challenge and salt, we compute a response using SHA256, and send it back. */
    bool auth_required;                     /* Does OBS need authentication? */
    char *challenge;                        /* Challenge string from OBS HELLO */
    char *salt;                             /* Salt string from OBS HELLO */
    
    /* === Optimization Cache ===
       We cache the current scene to avoid querying OBS unnecessarily. When we get
       a SceneChanged event, we update the cache. */
    char *current_scene;                    /* Cached name of active scene */
    pthread_mutex_t scene_mutex;            /* Protects the cache */
};

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Global initialization flag - tracks whether obsws_init() has been called.
   
   Why have global state at all? Some underlying libraries (like libwebsockets
   and OpenSSL) need one-time initialization. We do that in obsws_init() and
   make sure it only happens once, even if called multiple times. This flag
   tracks whether we've done it.
   
   We use a mutex to protect the flag because someone might call obsws_init()
   from multiple threads simultaneously. The mutex ensures only one thread does
   the initialization.
*/

static bool g_library_initialized = false;      /* Have we called the init code yet? */
static obsws_log_level_t g_log_level = OBSWS_LOG_INFO;  /* Global filtering level */
static obsws_debug_level_t g_debug_level = OBSWS_DEBUG_NONE;  /* Global debug verbosity */
static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;  /* Thread-safe initialization */

/* ============================================================================
 * Logging
 * ============================================================================ */

/* Internal logging function - core logging infrastructure.
   
   Design: We filter by log level (higher level = more verbose). If the message
   is below the current level, we don't even format it (saves CPU). If there's a
   user-provided callback, we use it; otherwise we print to stderr.
   
   Why two parameters (conn and format)? So we can log from both the main thread
   (with a connection object) and the global initialization code (without one).
*/

static void obsws_log(obsws_connection_t *conn, obsws_log_level_t level, const char *format, ...) {
    /* Early exit if this message is too verbose */
    if (level > g_log_level) {
        return;
    }
    
    /* Format the message using printf-style arguments */
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    /* Route to user callback or stderr. The callback lets the user handle logging
       however they want - write to file, send to logging service, etc. */
    if (conn && conn->config.log_callback) {
        conn->config.log_callback(level, message, conn->config.user_data);
    } else {
        const char *level_str[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG"};
        fprintf(stderr, "[OBSWS-%s] %s\n", level_str[level], message);
    }
}

/* Debug logging - finer control for protocol-level troubleshooting.
   
   Separate from regular logging because debug messages are very verbose and
   developers typically only enable them when debugging specific issues. The
   debug level goes 0-3, with higher levels including all output from lower levels.
   
   We use a larger buffer (4KB) because debug messages can include JSON payloads.
*/

static void obsws_debug(obsws_connection_t *conn, obsws_debug_level_t min_level, const char *format, ...) {
    /* Only output if global debug level is at or above the minimum for this message */
    if (g_debug_level < min_level) {
        return;
    }
    
    /* Format with a larger buffer for JSON and other verbose output */
    char message[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    /* Route through the callback as DEBUG-level logs */
    if (conn && conn->config.log_callback) {
        conn->config.log_callback(OBSWS_LOG_DEBUG, message, conn->config.user_data);
    } else {
        const char *debug_level_str[] = {"NONE", "LOW", "MED", "HIGH"};
        fprintf(stderr, "[DEBUG-%s] %s\n", debug_level_str[min_level], message);
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Generate a UUID v4 for request identification.
   
   UUIDs uniquely identify each request, so when a response comes back, we can match
   it to the original request. We use UUID v4 (random) because it's simple and the
   uniqueness probability is astronomically high.
   
   Note: This implementation uses rand() for simplicity. A production system might
   use /dev/urandom for better randomness, but the current approach is fine for
   most use cases. The protocol doesn't require cryptographically secure randomness.
   
   Format: 8-4-4-4-12 hex digits with dashes, exactly 36 characters.
   Example: 550e8400-e29b-41d4-a716-446655440000
   
   The version bits (0x4) and variant bits (0x8-b) mark this as a v4 UUID.
*/

static void generate_uuid(char *uuid_out) {
    unsigned int r1 = rand();
    unsigned int r2 = rand();
    unsigned int r3 = rand();
    unsigned int r4 = rand();
    
    sprintf(uuid_out, "%08x-%04x-%04x-%04x-%04x%08x",
            r1,                          /* 8 hex digits */
            r2 & 0xFFFF,                /* 4 hex digits */
            (r3 & 0x0FFF) | 0x4000,     /* 4 hex digits (set version 4) */
            (r4 & 0x3FFF) | 0x8000,     /* 4 hex digits (set variant bits) */
            rand() & 0xFFFF,            /* 4 hex digits */
            (unsigned int)rand());       /* 8 hex digits */
}

/* Base64 encode binary data using OpenSSL.
   
   Why base64 and not hex? Hex would be twice as large. Base64 is a standard
   encoding for binary data in text contexts (like WebSocket JSON messages).
   
   We use OpenSSL's BIO (Basic I/O) interface for encoding because it's robust
   and well-tested. The BIO_FLAGS_BASE64_NO_NL flag removes newlines that OpenSSL
   normally adds for readability - we don't want those in JSON.
*/

static char* base64_encode(const unsigned char *input, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *buffer_ptr;
    
    /* Set up OpenSSL base64 encoder: b64 filter pushing to memory buffer */
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    /* Disable newlines in output (OpenSSL adds them by default for readability) */
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    
    /* Encode the data */
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    /* Copy result to our own allocated buffer and null-terminate */
    char *result = malloc(buffer_ptr->length + 1);
    memcpy(result, buffer_ptr->data, buffer_ptr->length);
    result[buffer_ptr->length] = '\0';
    
    BIO_free_all(bio);
    return result;
}

/* Compute SHA256 hash of a null-terminated string.
   
   SHA256 is a cryptographic hash function. It's deterministic (same input always
   produces same output) and has an avalanche property (changing one bit in the
   input completely changes the output). This makes it perfect for authentication
   protocols.
   
   Why SHA256 instead of SHA1 or MD5? SHA256 is current-best-practice. SHA1 has
   known collisions, and MD5 is even more broken. SHA256 is secure for the
   foreseeable future.
   
   Why use EVP (Envelope) API instead of raw SHA256 functions? EVP is higher-level
   and more flexible - if we ever need to support different hash algorithms, we
   just change one line.
*/

static void sha256_hash(const char *input, unsigned char *output) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, input, strlen(input));
    EVP_DigestFinal_ex(ctx, output, NULL);
    EVP_MD_CTX_free(ctx);
}

/* Generate OBS WebSocket v5 authentication response using challenge-response protocol.
   
   OBS WebSocket v5 uses a two-step authentication protocol:
   1. Server sends challenge + salt
   2. Client computes: secret = base64(sha256(password + salt))
   3. Client computes: response = base64(sha256(secret + challenge))
   4. Client sends response
   5. Server verifies by computing the same thing
   
   Why this design? The password never travels over the network. Instead, a hash
   derived from the password (the secret) is combined with a fresh challenge each
   time, preventing replay attacks. This is similar to HTTP Digest Authentication.
   
   Why not use the password directly? That would be incredibly insecure. The
   two-step approach means an eavesdropper who sees the response can't use it
   again - the challenge was random and won't repeat.
*/

static char* generate_auth_response(const char *password, const char *salt, const char *challenge) {
    unsigned char secret_hash[SHA256_DIGEST_LENGTH];
    unsigned char auth_hash[SHA256_DIGEST_LENGTH];
    
    /* Step 1: Compute secret = base64(sha256(password + salt)) */
    char *password_salt = malloc(strlen(password) + strlen(salt) + 1);
    sprintf(password_salt, "%s%s", password, salt);
    sha256_hash(password_salt, secret_hash);
    free(password_salt);
    
    char *secret = base64_encode(secret_hash, SHA256_DIGEST_LENGTH);
    
    /* Step 2: Compute auth response = base64(sha256(secret + challenge)) */
    char *secret_challenge = malloc(strlen(secret) + strlen(challenge) + 1);
    sprintf(secret_challenge, "%s%s", secret, challenge);
    sha256_hash(secret_challenge, auth_hash);
    free(secret_challenge);
    free(secret);
    
    /* Return the final response, base64-encoded */
    char *auth_response = base64_encode(auth_hash, SHA256_DIGEST_LENGTH);
    return auth_response;
}

/* ============================================================================
 * State Management
 * ============================================================================ */

/* Update connection state and notify callback if state changed.
   
   This function is responsible for state transitions and notifying the user.
   We lock the mutex, make the change, unlock it, then call the callback without
   holding the lock. Why release the lock before calling the callback? Because
   the callback might take a long time, and we don't want to hold a lock during
   that time - it would prevent other threads from checking the state.
   
   We only call the callback if the state actually changed. This prevents spurious
   notifications if something tries to set the same state again.
*/

static void set_connection_state(obsws_connection_t *conn, obsws_state_t new_state) {
    /* Acquire lock, save old state, set new state, release lock */
    pthread_mutex_lock(&conn->state_mutex);
    obsws_state_t old_state = conn->state;
    conn->state = new_state;
    pthread_mutex_unlock(&conn->state_mutex);
    
    /* Call callback only if state actually changed (not a duplicate) */
    if (old_state != new_state && conn->config.state_callback) {
        conn->config.state_callback(conn, old_state, new_state, conn->config.user_data);
    }
    
    /* Log the transition for debugging/monitoring */
    obsws_log(conn, OBSWS_LOG_INFO, "State changed: %s -> %s", 
              obsws_state_string(old_state), obsws_state_string(new_state));
}

/* ============================================================================
 * Request Management
 * ============================================================================ */

/* Create a new pending request and add it to the tracking list.
   
   When we send a request to OBS, we need to track it so we can match the response
   when it arrives. This function creates a pending_request_t struct and adds it
   to the linked list. The request is initialized with the ID, a condition variable
   for waiting, and a current timestamp for timeout detection.
*/

static pending_request_t* create_pending_request(obsws_connection_t *conn, const char *request_id) {
    pending_request_t *req = calloc(1, sizeof(pending_request_t));
    if (!req) return NULL;
    
    /* Copy request ID and ensure null termination */
    strncpy(req->request_id, request_id, OBSWS_UUID_LENGTH - 1);
    req->request_id[OBSWS_UUID_LENGTH - 1] = '\0';
    
    /* Initialize request structure */
    req->response = calloc(1, sizeof(obsws_response_t));
    req->completed = false;
    req->timestamp = time(NULL);
    pthread_mutex_init(&req->mutex, NULL);
    pthread_cond_init(&req->cond, NULL);
    
    /* Add to linked list of pending requests */
    pthread_mutex_lock(&conn->requests_mutex);
    req->next = conn->pending_requests;
    conn->pending_requests = req;
    pthread_mutex_unlock(&conn->requests_mutex);
    
    return req;
}

/* Find a pending request by its UUID */
static pending_request_t* find_pending_request(obsws_connection_t *conn, const char *request_id) {
    pthread_mutex_lock(&conn->requests_mutex);
    pending_request_t *req = conn->pending_requests;
    
    /* Search linked list for matching request ID */
    while (req) {
        if (strcmp(req->request_id, request_id) == 0) {
            pthread_mutex_unlock(&conn->requests_mutex);
            return req;
        }
        req = req->next;
    }
    
    pthread_mutex_unlock(&conn->requests_mutex);
    return NULL;
}

/* Remove a pending request from the tracking list and free it */
static void remove_pending_request(obsws_connection_t *conn, pending_request_t *target) {
    pthread_mutex_lock(&conn->requests_mutex);
    pending_request_t **req = &conn->pending_requests;
    
    /* Find and remove from linked list */
    while (*req) {
        if (*req == target) {
            *req = target->next;
            pthread_mutex_unlock(&conn->requests_mutex);
            
            /* Clean up request resources */
            pthread_mutex_destroy(&target->mutex);
            pthread_cond_destroy(&target->cond);
            free(target);
            return;
        }
        req = &(*req)->next;
    }
    
    pthread_mutex_unlock(&conn->requests_mutex);
}

/* Clean up requests that have exceeded the timeout period */
static void cleanup_old_requests(obsws_connection_t *conn) {
    time_t now = time(NULL);
    pthread_mutex_lock(&conn->requests_mutex);
    
    pending_request_t **req = &conn->pending_requests;
    while (*req) {
        /* Check if request has timed out (30 seconds) */
        if (now - (*req)->timestamp > 30) {
            pending_request_t *old = *req;
            *req = old->next;
            
            /* Mark as completed with timeout error */
            pthread_mutex_lock(&old->mutex);
            old->completed = true;
            old->response->success = false;
            old->response->error_message = strdup("Request timeout");
            pthread_cond_broadcast(&old->cond);  /* Wake waiting threads */
            pthread_mutex_unlock(&old->mutex);
        } else {
            req = &(*req)->next;
        }
    }
    
    pthread_mutex_unlock(&conn->requests_mutex);
}

/* ============================================================================
 * WebSocket Protocol Handling
 * ============================================================================ */

/**
 * @brief Handle the initial HELLO handshake message from OBS.
 * 
 * When we first connect to OBS, the server sends a HELLO message containing
 * protocol version information and, if required, an authentication challenge
 * and salt. This function extracts that information and immediately responds
 * with an IDENTIFY message.
 * 
 * The authentication flow (if enabled) works as follows:
 * 1. Server sends HELLO with a random challenge string and a salt
 * 2. We compute: secret = base64(SHA256(password + salt))
 * 3. We compute: response = base64(SHA256(secret + challenge))
 * 4. We send this response in the IDENTIFY message
 * 5. If it matches what the server computed, auth succeeds
 * 
 * This challenge-response approach has several advantages over sending the
 * raw password:
 * - Password never travels across the network (only the computed response)
 * - If someone captures the network traffic, they can't replay the captured
 *   response because it's specific to this challenge (which was random)
 * - Similar to HTTP Digest Authentication (RFC 2617) but simpler
 * 
 * The function transitions the connection state from CONNECTING to AUTHENTICATING,
 * sends the IDENTIFY message, and logs any authentication requirements.
 * 
 * @param conn The connection structure containing buffers, config, and state
 * @param data The parsed JSON "d" field from the HELLO message, containing
 *             authentication info if auth is required
 * @return 0 on success, -1 on error (though errors are logged and connection
 *         continues - failure to authenticate will be detected by the server
 *         refusing to transition to IDENTIFIED state)
 * 
 * @internal
 */
static int handle_hello_message(obsws_connection_t *conn, cJSON *data) {
    /* DEBUG_LOW: Basic connection event */
    obsws_debug(conn, OBSWS_DEBUG_LOW, "Received Hello message from OBS");
    
    cJSON *auth = cJSON_GetObjectItem(data, "authentication");
    if (auth) {
        conn->auth_required = true;
        
        cJSON *challenge = cJSON_GetObjectItem(auth, "challenge");
        cJSON *salt = cJSON_GetObjectItem(auth, "salt");
        
        if (challenge && salt) {
            conn->challenge = strdup(challenge->valuestring);
            conn->salt = strdup(salt->valuestring);
            /* DEBUG_MEDIUM: Show auth parameters */
            obsws_debug(conn, OBSWS_DEBUG_MEDIUM, "Authentication required - salt: %s, challenge: %s", 
                     conn->salt, conn->challenge);
        }
    } else {
        conn->auth_required = false;
        obsws_debug(conn, OBSWS_DEBUG_LOW, "No authentication required");
    }
    
    /* Send Identify message */
    set_connection_state(conn, OBSWS_STATE_AUTHENTICATING);
    
    cJSON *identify = cJSON_CreateObject();
    cJSON_AddNumberToObject(identify, "op", OBSWS_OPCODE_IDENTIFY);
    
    cJSON *identify_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(identify_data, "rpcVersion", OBSWS_PROTOCOL_VERSION);
    cJSON_AddNumberToObject(identify_data, "eventSubscriptions", OBSWS_EVENT_ALL);
    
    if (conn->auth_required && conn->config.password) {
        /* DEBUG_HIGH: Show password being used */
        obsws_debug(conn, OBSWS_DEBUG_HIGH, "Generating auth response with password: '%s'", conn->config.password);
        char *auth_response = generate_auth_response(conn->config.password, conn->salt, conn->challenge);
        /* DEBUG_MEDIUM: Show generated auth string */
        obsws_debug(conn, OBSWS_DEBUG_MEDIUM, "Generated auth response: '%s'", auth_response);
        cJSON_AddStringToObject(identify_data, "authentication", auth_response);
        free(auth_response);
    } else {
        if (conn->auth_required) {
            obsws_log(conn, OBSWS_LOG_ERROR, "Authentication required but no password provided!");
        }
    }
    
    cJSON_AddItemToObject(identify, "d", identify_data);
    
    char *message = cJSON_PrintUnformatted(identify);
    cJSON_Delete(identify);
    
    /* DEBUG_HIGH: Show full Identify message */
    obsws_debug(conn, OBSWS_DEBUG_HIGH, "Sending Identify message: %s", message);
    
    pthread_mutex_lock(&conn->send_mutex);
    size_t len = strlen(message);
    if (len < conn->send_buffer_size - LWS_PRE) {
        memcpy(conn->send_buffer + LWS_PRE, message, len);
        int written = lws_write(conn->wsi, (unsigned char *)(conn->send_buffer + LWS_PRE), len, LWS_WRITE_TEXT);
        /* DEBUG_HIGH: Show bytes sent */
        obsws_debug(conn, OBSWS_DEBUG_HIGH, "Sent %d bytes (requested %zu)", written, len);
    } else {
        obsws_log(conn, OBSWS_LOG_ERROR, "Message too large for send buffer: %zu bytes", len);
    }
    pthread_mutex_unlock(&conn->send_mutex);
    
    free(message);
    return 0;
}

/**
 * @brief Handle the IDENTIFIED confirmation message from OBS.
 * 
 * After we send an IDENTIFY message with authentication (or without if auth
 * isn't required), OBS responds with an IDENTIFIED message to confirm that
 * the connection is established and ready for commands. This function marks
 * the connection as fully established, records connection statistics, and
 * resets the reconnection state.
 * 
 * Receiving this message means:
 * - Authentication succeeded (if it was required)
 * - The server has accepted our protocol version
 * - We can now send REQUEST messages and receive EVENT messages
 * - The connection is in a healthy state
 * 
 * We take this opportunity to:
 * 1. Log successful authentication
 * 2. Transition state to CONNECTED (the only valid way to enter this state)
 * 3. Record the timestamp of successful connection (for statistics)
 * 4. Reset the reconnection attempt counter and delay (we're connected!)
 * 
 * @param conn The connection structure to mark as identified
 * @param data Unused (the IDENTIFIED message typically has no data payload)
 * @return Always 0 (this message type should never fail)
 * 
 * @internal
 */
static int handle_identified_message(obsws_connection_t *conn, cJSON *data) {
    (void)data;  /* Unused parameter */
    obsws_log(conn, OBSWS_LOG_INFO, "Successfully authenticated with OBS");
    /* DEBUG_LOW: Authentication success */
    obsws_debug(conn, OBSWS_DEBUG_LOW, "Identified message received - authentication successful");
    set_connection_state(conn, OBSWS_STATE_CONNECTED);
    
    pthread_mutex_lock(&conn->stats_mutex);
    conn->stats.connected_since = time(NULL);
    conn->stats.reconnect_count = conn->reconnect_attempts;
    pthread_mutex_unlock(&conn->stats_mutex);
    
    conn->reconnect_attempts = 0;
    conn->current_reconnect_delay = conn->config.reconnect_delay_ms;
    
    return 0;
}

/**
 * @brief Handle EVENT messages from OBS (real-time notifications).
 * 
 * OBS continuously sends EVENT messages whenever something happens in the
 * application (scene changes, source muted/unmuted, recording started, etc.).
 * These events are only delivered if we subscribed to them in the IDENTIFY
 * message using the eventSubscriptions bitmask.
 * 
 * This function:
 * 1. Extracts the event type and event data from the JSON
 * 2. Calls the user's event_callback if one was configured (async event loop)
 * 3. Updates internal caches (e.g., current scene name on SceneChanged)
 * 
 * Important threading note: This is called from the background event_thread,
 * NOT from the main application thread. Therefore:
 * - The event_callback is executed in the event thread context
 * - The callback should not block (keep processing fast)
 * - The callback should not make blocking library calls
 * - The event_data_str parameter is temporary and freed after callback returns
 * - If the callback needs to keep the data, it must copy it
 * 
 * Scene caching optimization:
 * We maintain a cache of the currently active scene name in the connection
 * structure. When we see a CurrentProgramSceneChanged event, we update this
 * cache immediately. This avoids the need for the application to call
 * obsws_send_request(..., "GetCurrentProgramScene", ...) repeatedly.
 * The cache is protected by scene_mutex for thread safety.
 * 
 * @param conn The connection that received the event
 * @param data The parsed JSON "d" field containing eventType and eventData
 * @return 0 on success, -1 if the event data is malformed
 * 
 * @internal
 */
static int handle_event_message(obsws_connection_t *conn, cJSON *data) {
    cJSON *event_type = cJSON_GetObjectItem(data, "eventType");
    cJSON *event_data = cJSON_GetObjectItem(data, "eventData");
    
    /* DEBUG_MEDIUM: Show event type */
    if (event_type) {
        obsws_debug(conn, OBSWS_DEBUG_MEDIUM, "Event received: %s", event_type->valuestring);
    }
    
    if (event_type && conn->config.event_callback) {
        char *event_data_str = event_data ? cJSON_PrintUnformatted(event_data) : NULL;
        /* DEBUG_HIGH: Show full event data */
        if (event_data_str) {
            obsws_debug(conn, OBSWS_DEBUG_HIGH, "Event data: %s", event_data_str);
        }
        conn->config.event_callback(conn, event_type->valuestring, event_data_str, conn->config.user_data);
        if (event_data_str) free(event_data_str);
    }
    
    /* Update current scene cache if scene changed */
    if (event_type && strcmp(event_type->valuestring, "CurrentProgramSceneChanged") == 0) {
        cJSON *scene_name = cJSON_GetObjectItem(event_data, "sceneName");
        if (scene_name) {
            pthread_mutex_lock(&conn->scene_mutex);
            free(conn->current_scene);
            conn->current_scene = strdup(scene_name->valuestring);
            pthread_mutex_unlock(&conn->scene_mutex);
            /* DEBUG_LOW: Scene changes are important */
            obsws_debug(conn, OBSWS_DEBUG_LOW, "Scene changed to: %s", scene_name->valuestring);
        }
    }
    
    return 0;
}

/**
 * @brief Handle REQUEST_RESPONSE messages from OBS (responses to our commands).
 * 
 * When we send a REQUEST message (via obsws_send_request), OBS processes it
 * and sends back a REQUEST_RESPONSE message with the same requestId that we
 * used. This function matches the response to the pending request, populates
 * the response data, and wakes up the waiting thread.
 * 
 * The async request/response pattern allows the application to send multiple
 * requests without waiting for each response. The flow is:
 * 1. Application calls obsws_send_request("GetScenes", ...) -> returns immediately
 * 2. The request is created with a unique UUID and added to pending_requests list
 * 3. Background thread sends the request to OBS
 * 4. Background thread waits for response (on condition variable, not busy-polling)
 * 5. OBS responds with REQUEST_RESPONSE containing the requestId
 * 6. This function matches it to the pending request
 * 7. Function sets response->success, response->status_code, response->response_data
 * 8. Function signals the condition variable to wake the waiting thread
 * 9. Application thread wakes up with the response ready
 * 
 * This is far more efficient than synchronous request/response because:
 * - No blocking wait in the background thread
 * - Multiple requests can be in-flight simultaneously
 * - Doesn't freeze the application while waiting for OBS
 * - Condition variables are more efficient than polling
 * 
 * Response structure contains:
 * - success: Did the operation succeed? (not the HTTP status, but "was it valid?")
 * - status_code: The OBS response code (0 = success, >0 = error)
 * - response_data: JSON string with the actual result (e.g., scene list)
 * - error_message: If something failed, what was the reason?
 * 
 * @param conn The connection that received the response
 * @param data The parsed JSON "d" field containing requestId, requestStatus, etc.
 * @return 0 on success, -1 if the response is malformed (e.g., missing requestId)
 * 
 * @internal
 */
static int handle_request_response_message(obsws_connection_t *conn, cJSON *data) {
    cJSON *request_id = cJSON_GetObjectItem(data, "requestId");
    if (!request_id) return -1;
    
    /* DEBUG_MEDIUM: Show request ID being processed */
    obsws_debug(conn, OBSWS_DEBUG_MEDIUM, "Response received for request: %s", request_id->valuestring);
    
    pending_request_t *req = find_pending_request(conn, request_id->valuestring);
    if (!req) {
        obsws_log(conn, OBSWS_LOG_WARNING, "Received response for unknown request: %s", request_id->valuestring);
        return -1;
    }
    
    pthread_mutex_lock(&req->mutex);
    
    cJSON *request_status = cJSON_GetObjectItem(data, "requestStatus");
    if (request_status) {
        cJSON *result = cJSON_GetObjectItem(request_status, "result");
        cJSON *code = cJSON_GetObjectItem(request_status, "code");
        cJSON *comment = cJSON_GetObjectItem(request_status, "comment");
        
        req->response->success = result ? result->valueint : false;
        req->response->status_code = code ? code->valueint : -1;
        
        if (comment) {
            req->response->error_message = strdup(comment->valuestring);
        }
    }
    
    cJSON *response_data = cJSON_GetObjectItem(data, "responseData");
    if (response_data) {
        req->response->response_data = cJSON_PrintUnformatted(response_data);
    }
    
    req->completed = true;
    pthread_cond_broadcast(&req->cond);
    pthread_mutex_unlock(&req->mutex);
    
    return 0;
}

/**
 * @brief Route incoming WebSocket messages to appropriate handlers based on opcode.
 * 
 * Every message from OBS contains an "op" field (opcode) that identifies the
 * message type. This function:
 * 1. Parses the JSON to extract the opcode and data
 * 2. Routes to the appropriate handler function based on the opcode
 * 3. Updates statistics (messages_received, bytes_received)
 * 
 * The OBS WebSocket protocol uses these opcodes:
 * - HELLO (0): Server greeting with auth info - handled by handle_hello_message
 * - IDENTIFY (1): Client auth - we send this, don't receive it
 * - IDENTIFIED (2): Auth success - handled by handle_identified_message
 * - EVENT (5): Real-time notifications - handled by handle_event_message
 * - REQUEST_RESPONSE (7): Command responses - handled by handle_request_response_message
 * - Other opcodes like REIDENTIFY, batch operations: not currently handled
 * 
 * This is one of the most critical functions in the library because it's in
 * the hot path of message processing. Performance matters here. We keep it
 * lightweight and defer heavy processing to the specific handlers.
 * 
 * Error handling is conservative: malformed JSON or missing opcode doesn't
 * crash the connection, it just logs and continues. This allows us to be
 * resilient to protocol variations or corruption.
 * 
 * @param conn The connection that received the message
 * @param message Pointer to the raw message data (not null-terminated)
 * @param len Number of bytes in the message
 * @return 0 on success, -1 on parse error (does not disconnect)
 * 
 * @internal
 */
static int handle_websocket_message(obsws_connection_t *conn, const char *message, size_t len) {
    /* DEBUG_HIGH: Show full message content */
    obsws_debug(conn, OBSWS_DEBUG_HIGH, "Received message (%zu bytes): %.*s", len, (int)len, message);
    
    cJSON *json = cJSON_ParseWithLength(message, len);
    if (!json) {
        obsws_log(conn, OBSWS_LOG_ERROR, "Failed to parse JSON message");
        return -1;
    }
    
    cJSON *op = cJSON_GetObjectItem(json, "op");
    cJSON *data = cJSON_GetObjectItem(json, "d");
    
    if (!op) {
        obsws_log(conn, OBSWS_LOG_ERROR, "Message missing 'op' field");
        cJSON_Delete(json);
        return -1;
    }
    
    /* DEBUG_MEDIUM: Show opcode being processed */
    obsws_debug(conn, OBSWS_DEBUG_MEDIUM, "Processing opcode: %d", op->valueint);
    
    int result = 0;
    switch (op->valueint) {
        case OBSWS_OPCODE_HELLO:
            result = handle_hello_message(conn, data);
            break;
        case OBSWS_OPCODE_IDENTIFIED:
            result = handle_identified_message(conn, data);
            break;
        case OBSWS_OPCODE_EVENT:
            result = handle_event_message(conn, data);
            break;
        case OBSWS_OPCODE_REQUEST_RESPONSE:
            result = handle_request_response_message(conn, data);
            break;
        default:
            obsws_log(conn, OBSWS_LOG_DEBUG, "Unhandled opcode: %d", op->valueint);
            break;
    }
    
    cJSON_Delete(json);
    
    pthread_mutex_lock(&conn->stats_mutex);
    conn->stats.messages_received++;
    conn->stats.bytes_received += len;
    pthread_mutex_unlock(&conn->stats_mutex);
    
    return result;
}

/* ============================================================================
 * libwebsockets Callbacks
 * ============================================================================ */

/**
 * @brief libwebsockets callback - routes WebSocket events to our handlers.
 * 
 * libwebsockets is an event-driven WebSocket library. It calls this callback
 * function whenever something happens (connection established, data received,
 * connection closed, etc.). The "reason" parameter identifies what happened.
 * 
 * We handle these key reasons:
 * - LWS_CALLBACK_CLIENT_ESTABLISHED: TCP/WebSocket handshake complete, ready for messages
 * - LWS_CALLBACK_CLIENT_RECEIVE: Data arrived from OBS
 * - LWS_CALLBACK_CLIENT_WRITEABLE: Socket is writable (less common with our design)
 * - LWS_CALLBACK_CLIENT_CONNECTION_ERROR: Connection failed (network error, bad host, etc.)
 * - LWS_CALLBACK_CLIENT_CLOSED: Connection closed normally
 * - LWS_CALLBACK_WSI_DESTROY: Cleanup callback
 * 
 * Important: This callback is called from the background event_thread, not
 * the main application thread. So it must be thread-safe and not block.
 * 
 * Message assembly: OBS WebSocket messages might arrive fragmented (multiple
 * packets). We accumulate them in recv_buffer and check lws_is_final_fragment()
 * to know when a complete message has arrived. Only then do we parse it.
 * 
 * Error handling: Connection errors and receive buffer overflows are logged
 * but don't crash. We just transition to ERROR state and let the connection
 * cleanup/reconnection logic handle recovery.
 * 
 * @param wsi The WebSocket instance (provided by libwebsockets)
 * @param reason The callback reason (LWS_CALLBACK_*)
 * @param user Our connection pointer (registered at context creation)
 * @param in Incoming data (for LWS_CALLBACK_CLIENT_RECEIVE)
 * @param len Size of incoming data
 * @return 0 for success, -1 for error (affects connection handling)
 * 
 * @internal
 */
static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    obsws_connection_t *conn = (obsws_connection_t *)user;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            obsws_log(conn, OBSWS_LOG_INFO, "WebSocket connection established");
            set_connection_state(conn, OBSWS_STATE_CONNECTING);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (conn->recv_buffer_used + len < conn->recv_buffer_size) {
                memcpy(conn->recv_buffer + conn->recv_buffer_used, in, len);
                conn->recv_buffer_used += len;
                
                if (lws_is_final_fragment(wsi)) {
                    handle_websocket_message(conn, conn->recv_buffer, conn->recv_buffer_used);
                    conn->recv_buffer_used = 0;
                }
            } else {
                obsws_log(conn, OBSWS_LOG_ERROR, "Receive buffer overflow");
                conn->recv_buffer_used = 0;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            /* Handle queued sends if needed */
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            obsws_log(conn, OBSWS_LOG_ERROR, "Connection error: %s", in ? (char *)in : "unknown");
            set_connection_state(conn, OBSWS_STATE_ERROR);
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            obsws_log(conn, OBSWS_LOG_INFO, "WebSocket connection closed (reason in 'in' param)");
            if (in && len > 0) {
                obsws_log(conn, OBSWS_LOG_INFO, "Close reason: %.*s", (int)len, (char*)in);
            }
            set_connection_state(conn, OBSWS_STATE_DISCONNECTED);
            break;
            
        case LWS_CALLBACK_WSI_DESTROY:
            conn->wsi = NULL;
            break;
            
        default:
            break;
    }
    
    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        "obs-websocket",
        lws_callback,
        0,
        OBSWS_DEFAULT_BUFFER_SIZE,
        0, /* id */
        NULL, /* user */
        0 /* tx_packet_size */
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

/* ============================================================================
 * Event Thread
 * ============================================================================ */

/**
 * @brief Background thread function that continuously processes WebSocket events.
 * 
 * Each connection has one background thread dedicated to processing WebSocket
 * messages and timers. The main application thread remains free to make requests
 * and do application work without blocking.
 * 
 * This thread:
 * 1. Calls lws_service() to pump the libwebsockets event loop (typically blocks
 *    for 50ms waiting for events, then processes them and returns)
 * 2. Periodically cleans up old/timed-out requests
 * 3. Sends keep-alive pings if configured (to detect dead connections)
 * 4. Exits gracefully when should_exit flag is set
 * 
 * Lifetime: This thread is created in obsws_connect() and destroyed in
 * obsws_disconnect() using pthread_join(). The should_exit flag is used to
 * signal the thread to stop, which it checks at the start of each loop.
 * 
 * The lws_service() call is the core of this loop. It:
 * - Waits up to 50ms for data from the network using select/poll
 * - If data arrives, invokes lws_callback to notify us
 * - Returns after ~50ms even if no data (so we stay responsive to should_exit)
 * 
 * This asynchronous design has several advantages:
 * - App thread isn't blocked waiting for responses
 * - Multiple requests can be in-flight simultaneously
 * - Events can be delivered to callbacks instantly (no polling delay)
 * - Automatic keep-alive pings keep the connection alive through firewalls
 * 
 * Memory note: Callbacks invoked from this thread have access to the same
 * connection object as the main thread, hence all the mutexes protecting
 * critical sections. The pending_request_t condition variables synchronize
 * request responses between this thread and application threads.
 * 
 * @param arg The obsws_connection_t* that this thread services
 * @return Always NULL (threads don't return values)
 * 
 * @internal
 */
static void* event_thread_func(void *arg) {
    obsws_connection_t *conn = (obsws_connection_t *)arg;
    
    bool should_continue = true;
    while (should_continue) {
        /* Check exit flag with mutex protection */
        pthread_mutex_lock(&conn->state_mutex);
        should_continue = !conn->should_exit;
        pthread_mutex_unlock(&conn->state_mutex);
        
        if (!should_continue) break;
        
        if (conn->lws_context) {
            lws_service(conn->lws_context, 50);
            
            /* Cleanup old requests periodically */
            cleanup_old_requests(conn);
            
            /* Handle keep-alive pings */
            if (conn->config.ping_interval_ms > 0 && conn->state == OBSWS_STATE_CONNECTED) {
                time_t now = time(NULL);
                if (now - conn->last_ping_sent >= conn->config.ping_interval_ms / 1000) {
                    if (conn->wsi) {
                        lws_callback_on_writable(conn->wsi);
                    }
                    conn->last_ping_sent = now;
                }
            }
        } else {
            struct timespec ts = {0, 50000000};  /* 50ms = 50,000,000 nanoseconds */
            nanosleep(&ts, NULL);
        }
    }
    
    return NULL;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * @brief Initialize the libwsv5 library.
 * 
 * This function must be called before creating any connections. It:
 * 1. Initializes OpenSSL (EVP library for hashing)
 * 2. Seeds the random number generator for UUID generation
 * 3. Sets the global g_library_initialized flag
 * 
 * Thread safety: This function is thread-safe. Multiple threads can call it
 * simultaneously, and only one will actually do the initialization (protected
 * by g_init_mutex). Subsequent calls are no-ops.
 * 
 * Note: obsws_connect() will call this automatically if you forget, so you
 * don't *have* to call it explicitly. But doing so allows you to initialize
 * in a controlled way, separate from connection creation.
 * 
 * Cleanup: When you're done with the library, call obsws_cleanup() to
 * deallocate resources. This is technically optional on program exit (the OS
 * cleans up anyway), but good practice for testing and library shutdown.
 * 
 * @return OBSWS_OK always (initialization cannot fail in the current design)
 * 
 * @see obsws_cleanup, obsws_connect
 */
obsws_error_t obsws_init(void) {
    pthread_mutex_lock(&g_init_mutex);
    
    if (g_library_initialized) {
        pthread_mutex_unlock(&g_init_mutex);
        return OBSWS_OK;
    }
    
    /* Initialize OpenSSL */
    OpenSSL_add_all_algorithms();
    
    /* Seed random number generator */
    srand(time(NULL));
    
    g_library_initialized = true;
    pthread_mutex_unlock(&g_init_mutex);
    
    return OBSWS_OK;
}

/**
 * @brief Clean up library resources.
 * 
 * Call this when you're done with the library to deallocate OpenSSL resources.
 * This is a counterpart to obsws_init().
 * 
 * Important: Make sure all obsws_connection_t objects have been disconnected
 * and freed via obsws_disconnect() before calling this. If not, you might have
 * dangling references and resource leaks.
 * 
 * Thread safety: This function is thread-safe and idempotent (safe to call
 * multiple times). It checks g_library_initialized before doing anything.
 * 
 * Note: This is optional on program exit because the OS will clean up memory
 * anyway. But it's good practice for:
 * - Library consumers that need clean shutdown
 * - Memory leak detectors / Valgrind tests
 * - Programs that unload the library
 * 
 * @see obsws_init
 */
void obsws_cleanup(void) {
    pthread_mutex_lock(&g_init_mutex);
    
    if (!g_library_initialized) {
        pthread_mutex_unlock(&g_init_mutex);
        return;
    }
    
    EVP_cleanup();
    g_library_initialized = false;
    
    pthread_mutex_unlock(&g_init_mutex);
}

/**
 * @brief Get the library version string.
 * 
 * Returns a semantic version string like "1.0.0" that identifies which
 * version of libwsv5 is being used. Useful for debugging and logging.
 * 
 * @return Pointer to static version string (don't free)
 */
const char* obsws_version(void) {
    return OBSWS_VERSION;
}

/**
 * @brief Set the global log level threshold.
 * 
 * The library logs various messages during operation. This function sets which
 * messages are displayed. All messages at the specified level and higher
 * severity are shown; lower severity messages are hidden.
 * 
 * Levels in increasing severity:
 * - OBSWS_LOG_DEBUG: Low-level diagnostic info (too verbose for production)
 * - OBSWS_LOG_INFO: General informational messages (usual choice)
 * - OBSWS_LOG_WARNING: Potentially problematic situations (degraded but working)
 * - OBSWS_LOG_ERROR: Error conditions that need attention
 * 
 * Example: If you call obsws_set_log_level(OBSWS_LOG_WARNING), you'll see
 * only WARNING and ERROR messages, but not INFO or DEBUG messages.
 * 
 * Thread safety: This modifies a global variable without locking, so if you
 * might call this from multiple threads, use synchronization externally.
 * 
 * @param level The minimum severity level to display
 * @see obsws_set_debug_level
 */
void obsws_set_log_level(obsws_log_level_t level) {
    g_log_level = level;
}

/**
 * @brief Set the global debug level.
 * 
 * Debug logging is separate from regular logging. It provides extremely
 * detailed trace information about the WebSocket protocol, message parsing,
 * authentication, etc. This is useful during development and troubleshooting.
 * 
 * Debug levels:
 * - OBSWS_DEBUG_NONE: No debug output (fastest)
 * - OBSWS_DEBUG_LOW: Major state transitions and connection events
 * - OBSWS_DEBUG_MEDIUM: Message types and handlers invoked
 * - OBSWS_DEBUG_HIGH: Full message content and every operation
 * 
 * Debug logging is independent of log level. You can have OBSWS_LOG_ERROR
 * set (hide non-error logs) but still see OBSWS_DEBUG_HIGH output.
 * 
 * Performance warning: OBSWS_DEBUG_HIGH produces enormous output and will
 * slow down the library significantly. Only use during debugging!
 * 
 * Thread safety: Same as obsws_set_log_level (modifies global without locking).
 * 
 * @param level The debug verbosity level
 * @see obsws_set_log_level, obsws_get_debug_level
 */
void obsws_set_debug_level(obsws_debug_level_t level) {
    g_debug_level = level;
}

/**
 * @brief Get the current debug level.
 * 
 * This is a read-only query - it doesn't change anything, just returns the
 * current global debug level that was set by obsws_set_debug_level().
 * 
 * Useful for conditional logging in your application, e.g.:
 * ```
 * if (obsws_get_debug_level() >= OBSWS_DEBUG_MEDIUM) {
 *     // do expensive trace operation
 * }
 * ```
 * 
 * @return The currently active debug level
 * @see obsws_set_debug_level
 */
obsws_debug_level_t obsws_get_debug_level(void) {
    return g_debug_level;
}

/**
 * @brief Initialize a connection configuration structure with safe defaults.
 * 
 * Before calling obsws_connect(), you create an obsws_config_t structure
 * with the connection parameters. This function initializes that structure
 * with sensible defaults so you only need to change what's different for
 * your use case.
 * 
 * Default values set:
 * - port: 4455 (OBS WebSocket v5 default port)
 * - use_ssl: false (OBS uses ws://, not wss://)
 * - connect_timeout_ms: 5000 (5 seconds to connect)
 * - recv_timeout_ms: 5000 (5 seconds to receive each message)
 * - send_timeout_ms: 5000 (5 seconds to send each message)
 * - ping_interval_ms: 10000 (send ping every 10 seconds)
 * - ping_timeout_ms: 5000 (expect pong within 5 seconds)
 * - auto_reconnect: true (reconnect automatically if connection drops)
 * - reconnect_delay_ms: 1000 (start with 1 second delay)
 * - max_reconnect_delay_ms: 30000 (max wait is 30 seconds)
 * - max_reconnect_attempts: 0 (infinite attempts)
 * 
 * After calling this, you typically set:
 * - config.host = "localhost" (where OBS is running)
 * - config.password = "your_password" (if OBS has auth enabled)
 * - config.event_callback = your_callback_func (to receive events)
 * 
 * @param config Pointer to structure to initialize (must not be NULL)
 * 
 * @see obsws_connect
 */
void obsws_config_init(obsws_config_t *config) {
    memset(config, 0, sizeof(obsws_config_t));
    
    config->port = 4455;
    config->use_ssl = false;
    config->connect_timeout_ms = 5000;
    config->recv_timeout_ms = 5000;
    config->send_timeout_ms = 5000;
    config->ping_interval_ms = 10000;
    config->ping_timeout_ms = 5000;
    config->auto_reconnect = true;
    config->reconnect_delay_ms = 1000;
    config->max_reconnect_delay_ms = 30000;
    config->max_reconnect_attempts = 0; /* Infinite */
}

/**
 * @brief Establish a connection to OBS.
 * 
 * This is the main entry point for using the library. You provide a configuration
 * structure (initialized with obsws_config_init and then customized), and this
 * function connects to OBS, authenticates if needed, and spawns a background
 * thread to handle incoming messages and events.
 * 
 * The function returns immediately - it doesn't wait for the connection to
 * complete. Instead, it:
 * 1. Creates a connection structure with the provided config
 * 2. Allocates buffers for sending and receiving messages
 * 3. Creates a libwebsockets context and connects to OBS
 * 4. Spawns a background event_thread to process WebSocket messages
 * 5. Returns the connection handle
 * 
 * Connection states: The connection progresses through states:
 * - DISCONNECTED -> CONNECTING (TCP handshake, WebSocket upgrade)
 * - CONNECTING -> AUTHENTICATING (receive HELLO, send IDENTIFY)
 * - AUTHENTICATING -> CONNECTED (receive IDENTIFIED)
 * 
 * You don't have to wait for CONNECTED state before calling obsws_send_request,
 * but requests sent while not connected will return OBSWS_ERROR_NOT_CONNECTED.
 * 
 * Memory ownership: The connection structure is allocated and owned by this
 * function. You must free it by calling obsws_disconnect(). Don't free it directly
 * with free() - that will cause memory leaks (threads won't be cleaned up properly).
 * 
 * Error cases:
 * - NULL config or config->host: Returns NULL
 * - libwebsockets context creation fails: Returns NULL and logs error
 * - Network connection fails: Returns valid pointer but connection stays in ERROR state
 * - Bad password: Returns valid pointer but stays in AUTHENTICATING (never reaches CONNECTED)
 * 
 * Note: This function calls obsws_init() automatically if the library isn't
 * already initialized.
 * 
 * @param config Pointer to initialized obsws_config_t with connection parameters
 * @return Pointer to new connection handle, or NULL if creation failed
 * 
 * @see obsws_disconnect, obsws_get_state, obsws_send_request
 */
obsws_connection_t* obsws_connect(const obsws_config_t *config) {
    if (!g_library_initialized) {
        obsws_init();
    }
    
    if (!config || !config->host) {
        return NULL;
    }
    
    obsws_connection_t *conn = calloc(1, sizeof(obsws_connection_t));
    if (!conn) return NULL;
    
    /* Copy configuration */
    memcpy(&conn->config, config, sizeof(obsws_config_t));
    if (config->host) conn->config.host = strdup(config->host);
    if (config->password) conn->config.password = strdup(config->password);
    
    /* Initialize mutexes */
    pthread_mutex_init(&conn->state_mutex, NULL);
    pthread_mutex_init(&conn->send_mutex, NULL);
    pthread_mutex_init(&conn->requests_mutex, NULL);
    pthread_mutex_init(&conn->stats_mutex, NULL);
    pthread_mutex_init(&conn->scene_mutex, NULL);
    
    /* Allocate buffers */
    conn->recv_buffer_size = OBSWS_DEFAULT_BUFFER_SIZE;
    conn->recv_buffer = malloc(conn->recv_buffer_size);
    conn->send_buffer_size = OBSWS_DEFAULT_BUFFER_SIZE;
    conn->send_buffer = malloc(conn->send_buffer_size);
    
    conn->state = OBSWS_STATE_DISCONNECTED;
    conn->current_reconnect_delay = config->reconnect_delay_ms;
    
    /* Create libwebsockets context */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    conn->lws_context = lws_create_context(&info);
    if (!conn->lws_context) {
        obsws_log(conn, OBSWS_LOG_ERROR, "Failed to create libwebsockets context");
        free(conn->recv_buffer);
        free(conn->send_buffer);
        free(conn);
        return NULL;
    }
    
    /* Connect to OBS */
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    
    ccinfo.context = conn->lws_context;
    ccinfo.address = conn->config.host;
    ccinfo.port = conn->config.port;
    ccinfo.path = "/";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = protocols[0].name;
    ccinfo.userdata = conn;
    
    if (config->use_ssl) {
        ccinfo.ssl_connection = LCCSCF_USE_SSL;
    }
    
    conn->wsi = lws_client_connect_via_info(&ccinfo);
    if (!conn->wsi) {
        obsws_log(conn, OBSWS_LOG_ERROR, "Failed to initiate connection");
        lws_context_destroy(conn->lws_context);
        free(conn->recv_buffer);
        free(conn->send_buffer);
        free(conn);
        return NULL;
    }
    
    /* Start event thread - protect flags with mutex */
    pthread_mutex_lock(&conn->state_mutex);
    conn->thread_running = true;
    conn->should_exit = false;
    pthread_mutex_unlock(&conn->state_mutex);
    
    pthread_create(&conn->event_thread, NULL, event_thread_func, conn);
    
    obsws_log(conn, OBSWS_LOG_INFO, "Connecting to OBS at %s:%d", config->host, config->port);
    
    return conn;
}

/**
 * @brief Disconnect from OBS and clean up connection resources.
 * 
 * This is the counterpart to obsws_connect(). It cleanly shuts down the connection,
 * stops the background event thread, and frees all allocated resources.
 * 
 * The function performs these steps:
 * 1. Signal the event_thread to stop by setting should_exit flag
 * 2. Wait for the event_thread to actually exit using pthread_join()
 * 3. Send a normal WebSocket close frame to OBS (if connected)
 * 4. Destroy the libwebsockets context
 * 5. Free all pending requests (they won't get responses now, but don't leak memory)
 * 6. Free buffers, config, authentication data
 * 7. Destroy all mutexes and condition variables
 * 8. Free the connection structure
 * 
 * After calling this, the connection pointer is invalid. Don't use it again.
 * 
 * Safe to call multiple times: If you call it twice, the second call will be
 * a no-op (because conn will be NULL).
 * 
 * Safe to call even if connection never fully established: If you disconnect
 * while in CONNECTING or AUTHENTICATING state, everything is still cleaned up.
 * 
 * Important: This function blocks until the event_thread exits. If you have
 * a callback that's blocked, this will deadlock. Make sure your callbacks
 * don't block!
 * 
 * @param conn The connection to close (can be NULL - safe to call)
 * 
 * @see obsws_connect
 */
void obsws_disconnect(obsws_connection_t *conn) {
    if (!conn) return;
    
    obsws_log(conn, OBSWS_LOG_INFO, "Disconnecting from OBS");
    
    /* Stop event thread - protect flag with mutex */
    pthread_mutex_lock(&conn->state_mutex);
    conn->should_exit = true;
    bool thread_was_running = conn->thread_running;
    pthread_mutex_unlock(&conn->state_mutex);
    
    if (thread_was_running) {
        pthread_join(conn->event_thread, NULL);
    }
    
    /* Close WebSocket - only if connected */
    if (conn->wsi && conn->state == OBSWS_STATE_CONNECTED) {
        lws_close_reason(conn->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    }
    
    /* Cleanup libwebsockets */
    if (conn->lws_context) {
        lws_context_destroy(conn->lws_context);
    }
    
    /* Free pending requests */
    pthread_mutex_lock(&conn->requests_mutex);
    pending_request_t *req = conn->pending_requests;
    while (req) {
        pending_request_t *next = req->next;
        if (req->response) {
            obsws_response_free(req->response);
        }
        pthread_mutex_destroy(&req->mutex);
        pthread_cond_destroy(&req->cond);
        free(req);
        req = next;
    }
    pthread_mutex_unlock(&conn->requests_mutex);
    
    /* Free resources */
    free(conn->recv_buffer);
    free(conn->send_buffer);
    free((char *)conn->config.host);
    free((char *)conn->config.password);
    free(conn->challenge);
    free(conn->salt);
    free(conn->current_scene);
    
    /* Destroy mutexes */
    pthread_mutex_destroy(&conn->state_mutex);
    pthread_mutex_destroy(&conn->send_mutex);
    pthread_mutex_destroy(&conn->requests_mutex);
    pthread_mutex_destroy(&conn->stats_mutex);
    pthread_mutex_destroy(&conn->scene_mutex);
    
    free(conn);
}

/**
 * @brief Check if a connection is actively connected to OBS.
 * 
 * Convenience function that returns true if the connection is in CONNECTED state
 * and false otherwise. Useful for checking before sending requests.
 * 
 * Thread-safe: This function locks the state_mutex before checking, so it's
 * safe to call from any thread.
 * 
 * Return value: The connection must be in OBSWS_STATE_CONNECTED to return true.
 * If it's CONNECTING, AUTHENTICATING, ERROR, or DISCONNECTED, this returns false.
 * 
 * @param conn The connection to check (NULL is safe - returns false)
 * @return true if connected, false otherwise
 * 
 * @see obsws_get_state
 */
bool obsws_is_connected(const obsws_connection_t *conn) {
    if (!conn) return false;
    
    /* Thread-safe state check */
    pthread_mutex_lock((pthread_mutex_t *)&conn->state_mutex);
    bool connected = (conn->state == OBSWS_STATE_CONNECTED);
    pthread_mutex_unlock((pthread_mutex_t *)&conn->state_mutex);
    
    return connected;
}

/**
 * @brief Get the current connection state.
 * 
 * Returns one of the connection states:
 * - OBSWS_STATE_DISCONNECTED: Not connected, idle
 * - OBSWS_STATE_CONNECTING: TCP connection established, waiting for WebSocket handshake
 * - OBSWS_STATE_AUTHENTICATING: WebSocket established, waiting for auth response
 * - OBSWS_STATE_CONNECTED: Connected and ready for requests
 * - OBSWS_STATE_ERROR: Connection encountered an error
 * 
 * State transitions normally follow this flow:
 * DISCONNECTED -> CONNECTING -> AUTHENTICATING -> CONNECTED
 * 
 * But with errors, you can also have:
 * (any state) -> ERROR
 * 
 * You don't usually need to call this - just try to send requests and check
 * the error code. But it's useful for monitoring and debugging.
 * 
 * Thread-safe: This function locks the state_mutex, so it's safe to call
 * from any thread.
 * 
 * @param conn The connection to check (NULL is safe - returns DISCONNECTED)
 * @return The current connection state
 * 
 * @see obsws_is_connected
 */
obsws_state_t obsws_get_state(const obsws_connection_t *conn) {
    if (!conn) return OBSWS_STATE_DISCONNECTED;
    
    pthread_mutex_lock((pthread_mutex_t *)&conn->state_mutex);
    obsws_state_t state = conn->state;
    pthread_mutex_unlock((pthread_mutex_t *)&conn->state_mutex);
    
    return state;
}

/**
 * @brief Retrieve performance and connectivity statistics.
 * 
 * The library maintains statistics about the connection:
 * - messages_sent / messages_received: Count of WebSocket messages
 * - bytes_sent / bytes_received: Total bytes transmitted/received
 * - connected_since: Timestamp of when we reached CONNECTED state
 * - reconnect_count: How many times we've reconnected (0 if never disconnected)
 * 
 * These can be useful for:
 * - Monitoring connection health
 * - Detecting stalled connections (if bytes_received stops increasing)
 * - Debugging and performance profiling
 * - Health dashboards or logging
 * 
 * Thread-safe: This function acquires stats_mutex and copies the entire
 * stats structure, so it's safe to call from any thread. The copy operation
 * is atomic from the caller's perspective.
 * 
 * Example usage:
 * ```
 * obsws_stats_t stats;
 * obsws_get_stats(conn, &stats);
 * printf("Received %zu messages, %zu bytes\\n", 
 *        stats.messages_received, stats.bytes_received);
 * ```
 * 
 * @param conn The connection to query (NULL returns error)
 * @param stats Pointer to stats structure to fill (must not be NULL)
 * @return OBSWS_OK on success, OBSWS_ERROR_INVALID_PARAM if conn or stats is NULL
 * 
 * @see obsws_stats_t
 */
obsws_error_t obsws_get_stats(const obsws_connection_t *conn, obsws_stats_t *stats) {
    if (!conn || !stats) return OBSWS_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock((pthread_mutex_t *)&conn->stats_mutex);
    memcpy(stats, &conn->stats, sizeof(obsws_stats_t));
    pthread_mutex_unlock((pthread_mutex_t *)&conn->stats_mutex);
    
    return OBSWS_OK;
}

/**
 * @brief Send a synchronous request to OBS and wait for the response.
 * 
 * This is the core function for all OBS operations. It implements the asynchronous
 * request-response pattern of the OBS WebSocket v5 protocol:
 * 
 * **Protocol Flow:**
 * 1. Generate a unique UUID for this request (used to match responses)
 * 2. Create a pending_request_t to track the in-flight operation
 * 3. Build the request JSON with opcode 6 (REQUEST)
 * 4. Send the message via lws_write()
 * 5. Block the caller with pthread_cond_timedwait() until response arrives
 * 6. Return the response to caller (who owns it and must free with obsws_response_free)
 * 
 * **Why synchronous from caller's perspective?**
 * Although WebSocket messages are async at the protocol level, we provide a
 * synchronous API - the caller sends a request and blocks until the response
 * arrives. This is simpler for application code than callback-based async APIs.
 * 
 * Behind the scenes, the background event_thread continuously processes WebSocket
 * messages. When a REQUEST_RESPONSE (opcode 7) arrives matching a pending request
 * ID, it signals the waiting condition variable, waking up the blocked caller.
 * 
 * **Performance implications:**
 * - Thread-safe: The main app thread can be blocked in obsws_send_request() while
 *   the background event_thread processes other messages
 * - No polling: Uses condition variables, not CPU-wasting polling loops
 * - Can make multiple simultaneous requests from different threads (up to
 *   OBSWS_MAX_PENDING_REQUESTS = 256)
 * 
 * **Example usage:**
 * ```
 * obsws_response_t *response = NULL;
 * obsws_error_t err = obsws_send_request(conn, "SetCurrentProgramScene",
 *                                        "{\"sceneName\": \"Scene1\"}", 
 *                                        &response, 0);
 * if (err == OBSWS_OK && response && response->success) {
 *     printf("Scene switched successfully\\n");
 * }
 * obsws_response_free(response);
 * ```
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param request_type OBS request type like "GetCurrentProgramScene", "SetCurrentProgramScene", etc.
 * @param request_data Optional JSON string with request parameters. NULL for no parameters.
 *                     Example: "{\"sceneName\": \"Scene1\"}"
 * @param response Output pointer for the response. Will be allocated by this function.
 *                 Caller must free with obsws_response_free(). Can be NULL if caller doesn't
 *                 need the response (but response is still consumed from server).
 * @param timeout_ms Timeout in milliseconds (0 = use config->recv_timeout_ms, typically 30000ms)
 * 
 * @return OBSWS_OK if response received (check response->success for operation success)
 * @return OBSWS_ERROR_INVALID_PARAM if conn, request_type, or response pointer is NULL
 * @return OBSWS_ERROR_NOT_CONNECTED if connection is not in CONNECTED state
 * @return OBSWS_ERROR_OUT_OF_MEMORY if pending request allocation fails
 * @return OBSWS_ERROR_SEND_FAILED if message send fails (buffer too small, invalid wsi, etc)
 * @return OBSWS_ERROR_TIMEOUT if no response received within timeout_ms
 * 
 * @see obsws_response_t, obsws_response_free, obsws_error_string
 */
obsws_error_t obsws_send_request(obsws_connection_t *conn, const char *request_type,
                                 const char *request_data, obsws_response_t **response, uint32_t timeout_ms) {
    if (!conn || !request_type || !response) {
        return OBSWS_ERROR_INVALID_PARAM;
    }
    
    if (conn->state != OBSWS_STATE_CONNECTED) {
        return OBSWS_ERROR_NOT_CONNECTED;
    }
    
    /* Generate request ID */
    char request_id[OBSWS_UUID_LENGTH];
    generate_uuid(request_id);
    
    /* Create pending request */
    pending_request_t *req = create_pending_request(conn, request_id);
    if (!req) {
        return OBSWS_ERROR_OUT_OF_MEMORY;
    }
    
    /* Build request JSON */
    cJSON *request = cJSON_CreateObject();
    cJSON_AddNumberToObject(request, "op", OBSWS_OPCODE_REQUEST);
    
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "requestType", request_type);
    cJSON_AddStringToObject(d, "requestId", request_id);
    
    if (request_data) {
        cJSON *data = cJSON_Parse(request_data);
        if (data) {
            cJSON_AddItemToObject(d, "requestData", data);
        }
    }
    
    cJSON_AddItemToObject(request, "d", d);
    
    char *message = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    
    /* DEBUG_HIGH: Show request being sent */
    obsws_debug(conn, OBSWS_DEBUG_HIGH, "Sending request (ID: %s): %s", request_id, message);
    
    /* Send request */
    pthread_mutex_lock(&conn->send_mutex);
    size_t len = strlen(message);
    obsws_error_t result = OBSWS_OK;
    
    if (len < conn->send_buffer_size - LWS_PRE && conn->wsi) {
        memcpy(conn->send_buffer + LWS_PRE, message, len);
        int written = lws_write(conn->wsi, (unsigned char *)(conn->send_buffer + LWS_PRE), len, LWS_WRITE_TEXT);
        
        if (written < 0) {
            result = OBSWS_ERROR_SEND_FAILED;
        } else {
            pthread_mutex_lock(&conn->stats_mutex);
            conn->stats.messages_sent++;
            conn->stats.bytes_sent += len;
            pthread_mutex_unlock(&conn->stats_mutex);
        }
    } else {
        result = OBSWS_ERROR_SEND_FAILED;
    }
    pthread_mutex_unlock(&conn->send_mutex);
    
    free(message);
    
    if (result != OBSWS_OK) {
        remove_pending_request(conn, req);
        return result;
    }
    
    /* Wait for response */
    if (timeout_ms == 0) {
        timeout_ms = conn->config.recv_timeout_ms;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&req->mutex);
    while (!req->completed) {
        int wait_result = pthread_cond_timedwait(&req->cond, &req->mutex, &ts);
        if (wait_result == ETIMEDOUT) {
            pthread_mutex_unlock(&req->mutex);
            remove_pending_request(conn, req);
            return OBSWS_ERROR_TIMEOUT;
        }
    }
    
    *response = req->response;
    req->response = NULL; /* Transfer ownership */
    pthread_mutex_unlock(&req->mutex);
    
    remove_pending_request(conn, req);
    
    return OBSWS_OK;
}

/**
 * @brief Switch OBS to a specific scene.
 * 
 * This is a high-level convenience function for scene switching. It demonstrates several
 * important design patterns in the library:
 * 
 * **Optimization: Scene Cache**
 * Before sending a request to OBS, this function checks the cached current scene. If the
 * requested scene is already active, it returns immediately without network overhead.
 * The cache is maintained by the event thread when SceneChanged events arrive.
 * 
 * **Memory Ownership**
 * If the caller provides response pointer, they receive ownership and must call
 * obsws_response_free(). If response is NULL, the function frees the response internally.
 * This flexibility allows three usage patterns:
 * 1. Check response: `obsws_set_current_scene(conn, name, &resp); if (resp->success) ...`
 * 2. Ignore response: `obsws_set_current_scene(conn, name, NULL);` (response is freed internally)
 * 3. Just check error: `if (obsws_send_request(...) != OBSWS_OK) ...`
 * 
 * **Example usage:**
 * ```
 * // Pattern 1: Check response details
 * obsws_response_t *response = NULL;
 * if (obsws_set_current_scene(conn, "Scene1", &response) == OBSWS_OK &&
 *     response && response->success) {
 *     printf("Switched successfully\\n");
 * } else {
 *     printf("Switch failed: %s\\n", response ? response->error_message : "unknown");
 * }
 * obsws_response_free(response);
 * 
 * // Pattern 2: Ignore response (simpler)
 * obsws_set_current_scene(conn, "Scene1", NULL);
 * ```
 * 
 * **Thread-safety:**
 * - Scene cache is protected by scene_mutex
 * - Safe to call from any thread
 * - Multiple calls can happen simultaneously (each uses send_mutex)
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param scene_name Name of the scene to switch to. Must not be NULL.
 * @param response Optional output for response details. If provided, caller owns it
 *                 and must free with obsws_response_free(). If NULL, response is
 *                 freed internally.
 * 
 * @return OBSWS_OK if request sent and response received (check response->success
 *         for whether the scene switch actually succeeded in OBS)
 * @return OBSWS_ERROR_INVALID_PARAM if conn or scene_name is NULL
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if no response from OBS
 * 
 * @see obsws_get_current_scene, obsws_response_t, obsws_response_free
 */
obsws_error_t obsws_set_current_scene(obsws_connection_t *conn, const char *scene_name, obsws_response_t **response) {
    if (!conn || !scene_name) {
        return OBSWS_ERROR_INVALID_PARAM;
    }
    
    /* Check cache to avoid redundant switches */
    pthread_mutex_lock(&conn->scene_mutex);
    bool already_current = (conn->current_scene && strcmp(conn->current_scene, scene_name) == 0);
    pthread_mutex_unlock(&conn->scene_mutex);
    
    if (already_current) {
        obsws_log(conn, OBSWS_LOG_DEBUG, "Already on scene: %s", scene_name);
        if (response) {
            *response = calloc(1, sizeof(obsws_response_t));
            (*response)->success = true;
        }
        return OBSWS_OK;
    }
    
    cJSON *request_data = cJSON_CreateObject();
    cJSON_AddStringToObject(request_data, "sceneName", scene_name);
    
    char *data_str = cJSON_PrintUnformatted(request_data);
    cJSON_Delete(request_data);
    
    obsws_response_t *resp = NULL;
    obsws_error_t result = obsws_send_request(conn, "SetCurrentProgramScene", data_str, &resp, 0);
    free(data_str);
    
    if (result == OBSWS_OK && resp && resp->success) {
        pthread_mutex_lock(&conn->scene_mutex);
        free(conn->current_scene);
        conn->current_scene = strdup(scene_name);
        pthread_mutex_unlock(&conn->scene_mutex);
        
        obsws_log(conn, OBSWS_LOG_INFO, "Switched to scene: %s", scene_name);
    }
    
    if (response) {
        *response = resp;
    } else if (resp) {
        obsws_response_free(resp);
    }
    
    return result;
}

/**
 * @brief Query the currently active scene in OBS.
 * 
 * This function queries OBS for the active scene name and returns it in the provided
 * buffer. It also updates the local scene cache to keep it synchronized.
 * 
 * **Cache Synchronization**
 * This function always queries the OBS server (doesn't use cached value). When the
 * response arrives, it updates the cache. This ensures the library's cached scene
 * name stays in sync with the actual OBS state.
 * 
 * **Buffer Management**
 * The caller provides a buffer. If the scene name is longer than buffer_size-1,
 * it will be truncated and null-terminated. Always check the returned buffer length
 * if you need the full name.
 * 
 * **Thread-safety**
 * The scene_mutex protects the cache update, so this is safe to call from any thread.
 * Multiple concurrent calls are safe but will all query OBS (no deduplication).
 * 
 * **Example usage:**
 * ```
 * char scene[256];
 * if (obsws_get_current_scene(conn, scene, sizeof(scene)) == OBSWS_OK) {
 *     printf("Current scene: %s\\n", scene);
 * }
 * ```
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param scene_name Buffer to receive the scene name (must not be NULL)
 * @param buffer_size Size of scene_name buffer (must be > 0)
 * 
 * @return OBSWS_OK if scene name retrieved successfully
 * @return OBSWS_ERROR_INVALID_PARAM if conn, scene_name is NULL or buffer_size is 0
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if OBS doesn't respond
 * @return OBSWS_ERROR_PARSE_FAILED if response can't be parsed
 * 
 * @see obsws_set_current_scene
 */
obsws_error_t obsws_get_current_scene(obsws_connection_t *conn, char *scene_name, size_t buffer_size) {
    if (!conn || !scene_name || buffer_size == 0) {
        return OBSWS_ERROR_INVALID_PARAM;
    }
    
    obsws_response_t *response = NULL;
    obsws_error_t result = obsws_send_request(conn, "GetCurrentProgramScene", NULL, &response, 0);
    
    if (result == OBSWS_OK && response && response->success && response->response_data) {
        cJSON *data = cJSON_Parse(response->response_data);
        if (data) {
            cJSON *name = cJSON_GetObjectItem(data, "currentProgramSceneName");
            if (name && name->valuestring) {
                strncpy(scene_name, name->valuestring, buffer_size - 1);
                scene_name[buffer_size - 1] = '\0';
                
                /* Update cache */
                pthread_mutex_lock(&conn->scene_mutex);
                free(conn->current_scene);
                conn->current_scene = strdup(name->valuestring);
                pthread_mutex_unlock(&conn->scene_mutex);
            }
            cJSON_Delete(data);
        }
    }
    
    if (response) {
        obsws_response_free(response);
    }
    
    return result;
}

/**
 * @brief Start recording in OBS.
 * 
 * Tells OBS to begin recording the current scene composition to disk. The recording
 * path and format are determined by OBS settings, not by this library.
 * 
 * This is a convenience wrapper around obsws_send_request() using the OBS
 * "StartRecord" request type.
 * 
 * **Return value interpretation:**
 * - OBSWS_OK: Request was sent and OBS responded (check response->success)
 * - Other errors: Network/connection problem
 * 
 * **Example usage:**
 * ```
 * obsws_response_t *resp = NULL;
 * if (obsws_start_recording(conn, &resp) == OBSWS_OK && resp && resp->success) {
 *     printf("Recording started\\n");
 * }
 * obsws_response_free(resp);
 * ```
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param response Optional output for response. Caller owns if provided, must free.
 * 
 * @return OBSWS_OK if response received (check response->success for success)
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if OBS doesn't respond
 * 
 * @see obsws_stop_recording, obsws_send_request, obsws_response_free
 */
obsws_error_t obsws_start_recording(obsws_connection_t *conn, obsws_response_t **response) {
    return obsws_send_request(conn, "StartRecord", NULL, response, 0);
}

/**
 * @brief Stop recording in OBS.
 * 
 * Tells OBS to stop the currently active recording. If no recording is in progress,
 * OBS returns success anyway (idempotent operation).
 * 
 * This is a convenience wrapper around obsws_send_request() using the OBS
 * "StopRecord" request type.
 * 
 * **Example usage:**
 * ```
 * if (obsws_stop_recording(conn, NULL) != OBSWS_OK) {
 *     fprintf(stderr, "Failed to stop recording\\n");
 * }
 * ```
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param response Optional output for response. Caller owns if provided, must free.
 * 
 * @return OBSWS_OK if response received (check response->success for success)
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if OBS doesn't respond
 * 
 * @see obsws_start_recording, obsws_send_request, obsws_response_free
 */
obsws_error_t obsws_stop_recording(obsws_connection_t *conn, obsws_response_t **response) {
    return obsws_send_request(conn, "StopRecord", NULL, response, 0);
}

/**
 * @brief Start streaming in OBS.
 * 
 * Tells OBS to begin streaming to the configured destination (Twitch, YouTube, etc).
 * The stream settings (URL, key, bitrate, etc) are determined by OBS settings.
 * 
 * This is a convenience wrapper around obsws_send_request() using the OBS
 * "StartStream" request type.
 * 
 * **Thread-safe:** Safe to call from any thread while connected.
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param response Optional output for response. Caller owns if provided, must free.
 * 
 * @return OBSWS_OK if response received (check response->success for success)
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if OBS doesn't respond
 * 
 * @see obsws_stop_streaming, obsws_send_request, obsws_response_free
 */
obsws_error_t obsws_start_streaming(obsws_connection_t *conn, obsws_response_t **response) {
    return obsws_send_request(conn, "StartStream", NULL, response, 0);
}

/**
 * @brief Stop streaming in OBS.
 * 
 * Tells OBS to stop the active stream. If not currently streaming, OBS returns
 * success anyway (idempotent operation).
 * 
 * This is a convenience wrapper around obsws_send_request() using the OBS
 * "StopStream" request type.
 * 
 * @param conn Connection object (must be in CONNECTED state)
 * @param response Optional output for response. Caller owns if provided, must free.
 * 
 * @return OBSWS_OK if response received (check response->success for success)
 * @return OBSWS_ERROR_NOT_CONNECTED if connection not ready
 * @return OBSWS_ERROR_TIMEOUT if OBS doesn't respond
 * 
 * @see obsws_start_streaming, obsws_send_request, obsws_response_free
 */
obsws_error_t obsws_stop_streaming(obsws_connection_t *conn, obsws_response_t **response) {
    return obsws_send_request(conn, "StopStream", NULL, response, 0);
}

/**
 * @brief Free a response object previously allocated by obsws_send_request().
 * 
 * This function safely deallocates all memory associated with a response:
 * - The error_message string (if present)
 * - The response_data JSON string (if present)  
 * - The response structure itself
 * 
 * **Safe to call with NULL**
 * Calling with NULL is safe and does nothing - no crash or error.
 * This allows for simpler cleanup:
 * ```
 * obsws_response_t *resp = NULL;
 * obsws_send_request(..., &resp, ...);
 * obsws_response_free(resp);  // Safe even if send_request failed
 * ```
 * 
 * **Memory ownership**
 * - obsws_send_request() allocates the response - you must free it
 * - High-level functions like obsws_set_current_scene() can optionally take
 *   response ownership or free internally based on parameters
 * 
 * **NOT thread-safe**
 * Each response should only be accessed/freed by one thread. If multiple threads
 * need the response, use higher-level synchronization.
 * 
 * @param response Response to free. Can be NULL (does nothing if so).
 * 
 * @see obsws_send_request, obsws_response_t
 */
void obsws_response_free(obsws_response_t *response) {
    if (!response) return;
    
    free(response->error_message);
    free(response->response_data);
    free(response);
}

/**
 * @brief Convert an error code to a human-readable string.
 * 
 * Utility function for error reporting and logging. Returns a brief English
 * description of each error code.
 * 
 * **Never returns NULL**
 * Unknown error codes return "Unknown error", so it's always safe to use
 * the returned pointer without NULL checks.
 * 
 * **Example usage:**
 * ```
 * obsws_error_t err = obsws_send_request(...);
 * if (err != OBSWS_OK) {
 *     fprintf(stderr, "Error: %s\\n", obsws_error_string(err));
 * }
 * ```
 * 
 * **Strings are constants**
 * Returned strings are statically allocated - do not modify or free them.
 * 
 * @param error The error code to describe
 * @return Pointer to a static string describing the error
 * 
 * @see obsws_error_t
 */
const char* obsws_error_string(obsws_error_t error) {
    switch (error) {
        case OBSWS_OK: return "Success";
        case OBSWS_ERROR_INVALID_PARAM: return "Invalid parameter";
        case OBSWS_ERROR_CONNECTION_FAILED: return "Connection failed";
        case OBSWS_ERROR_AUTH_FAILED: return "Authentication failed";
        case OBSWS_ERROR_TIMEOUT: return "Timeout";
        case OBSWS_ERROR_SEND_FAILED: return "Send failed";
        case OBSWS_ERROR_RECV_FAILED: return "Receive failed";
        case OBSWS_ERROR_PARSE_FAILED: return "Parse failed";
        case OBSWS_ERROR_NOT_CONNECTED: return "Not connected";
        case OBSWS_ERROR_ALREADY_CONNECTED: return "Already connected";
        case OBSWS_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case OBSWS_ERROR_SSL_FAILED: return "SSL failed";
        default: return "Unknown error";
    }
}

/**
 * @brief Convert a connection state to a human-readable string.
 * 
 * Utility function for logging and debugging. Returns a brief English description
 * of each connection state.
 * 
 * **State Transitions:**
 * - DISCONNECTED: Initial state or after disconnect()
 * - CONNECTING: connect() called, establishing TCP connection
 * - AUTHENTICATING: TCP connected, performing challenge-response auth
 * - CONNECTED: Auth succeeded, ready for requests
 * - ERROR: Network error or protocol failure, should reconnect
 * 
 * **Valid transitions:**
 * ```
 * DISCONNECTED -> CONNECTING -> AUTHENTICATING -> CONNECTED
 * CONNECTED -> DISCONNECTED (on explicit disconnect)
 * CONNECTED -> ERROR (on network failure)
 * ERROR -> CONNECTING (on reconnect attempt)
 * ```
 * 
 * **Example usage:**
 * ```
 * obsws_state_t state = obsws_get_state(conn);
 * printf("Connection state: %s\\n", obsws_state_string(state));
 * ```
 * 
 * Never returns NULL - unknown states return "Unknown".
 * Returned strings are static - do not modify or free.
 * 
 * @param state The connection state to describe
 * @return Pointer to a static string describing the state
 * 
 * @see obsws_get_state, obsws_state_t, obsws_is_connected
 */
const char* obsws_state_string(obsws_state_t state) {
    switch (state) {
        case OBSWS_STATE_DISCONNECTED: return "Disconnected";
        case OBSWS_STATE_CONNECTING: return "Connecting";
        case OBSWS_STATE_AUTHENTICATING: return "Authenticating";
        case OBSWS_STATE_CONNECTED: return "Connected";
        case OBSWS_STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}

/**
 * @brief Process pending WebSocket events (compatibility function).
 * 
 * This function is provided for API compatibility with single-threaded applications.
 * However, the libwsv5 library uses a background event_thread by default, so this
 * function is usually not needed - events are processed automatically in the background.
 * 
 * **Background Event Processing:**
 * By design, all WebSocket messages (events, responses, etc.) are processed by the
 * background event_thread. This thread:
 * - Continuously calls lws_service() to pump WebSocket events
 * - Receives incoming messages from OBS
 * - Routes responses to waiting callers via condition variables
 * - Calls event callbacks for real-time events
 * - Maintains keep-alive pings
 * 
 * Applications don't need to call this function - the thread handles everything.
 * 
 * **What this function does:**
 * Currently, this is mainly for API compatibility. It:
 * - Validates the connection object
 * - If timeout_ms > 0, sleeps for that duration
 * - Returns 0 (success)
 * 
 * **When to use:**
 * - Most applications: Don't call this - use background thread
 * - If you disable background thread: Call this in your main loop
 * 
 * **Example (not typical - background thread is recommended):**
 * ```
 * // Not recommended - background thread is better
 * while (app_running) {
 *     obsws_process_events(conn, 100);  // Check every 100ms
 * }
 * ```
 * 
 * Better approach - let background thread handle it:
 * ```
 * obsws_connect(conn, "localhost", 4455, "password");
 * // Background thread processes events automatically
 * while (app_running) {
 *     // Your application code - no need to call process_events
 * }
 * obsws_disconnect(conn);
 * ```
 * 
 * @param conn Connection object (can be NULL - returns error)
 * @param timeout_ms Sleep duration in milliseconds (0 = don't sleep)
 * 
 * @return 0 on success
 * @return OBSWS_ERROR_INVALID_PARAM if conn is NULL
 * 
 * @see obsws_connect, obsws_disconnect, event_thread_func
 */
int obsws_process_events(obsws_connection_t *conn, uint32_t timeout_ms) {
    if (!conn) return OBSWS_ERROR_INVALID_PARAM;
    
    /* Events are processed in the background thread */
    /* This function is provided for API compatibility */
    if (timeout_ms > 0) {
        struct timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        nanosleep(&ts, NULL);
    }
    
    return 0;
}
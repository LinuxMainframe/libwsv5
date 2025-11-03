/*
 * libwsv5.h - OBS WebSocket v5 Protocol Library
 * 
 * A robust C library for managing OBS connections via the WebSocket v5 protocol.
 * Supports multiple concurrent connections, automatic reconnection, and thread-safe
 * operations for streaming, recording, scene control, and live production setups.
 * 
 * Author: Aidan A. Bradley
 * Maintainer: Aidan A. Bradley
 * License: MIT
 * 
 * Key Features:
 * - Connection management with automatic reconnection
 * - OBS WebSocket v5 authentication
 * - Scene and source control
 * - Recording and streaming control
 * - Connection health monitoring
 * - Thread-safe multi-connection support
 * - Comprehensive error handling
 */

#ifndef LIBWSV5_LIBRARY_H
#define LIBWSV5_LIBRARY_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes returned by library functions.
 * 
 * These error codes provide detailed information about what went wrong during
 * library operations. Unlike generic error codes, they help distinguish between
 * different failure modes so you can implement proper error handling and recovery
 * strategies. For example, OBSWS_ERROR_TIMEOUT means you should probably retry
 * the operation, while OBSWS_ERROR_AUTH_FAILED means retrying won't help - the
 * password is just wrong.
 * 
 * The library uses negative error codes following POSIX conventions. Zero is always
 * success. This makes it easy to check errors with simple conditions like if (error < 0).
 * 
 * Note that some errors are recoverable (network timeouts, temporary connection
 * failures) while others are not (invalid parameters, authentication failure).
 * The auto-reconnect feature only applies to network-level errors, not application
 * errors like wrong scene names.
 */
typedef enum {
    OBSWS_OK = 0,
    
    /* Parameter validation errors (application layer) - not recoverable by retrying */
    OBSWS_ERROR_INVALID_PARAM = -1,
    
    /* Network-level errors (can be recovered with reconnection) */
    OBSWS_ERROR_CONNECTION_FAILED = -2,
    OBSWS_ERROR_SEND_FAILED = -5,
    OBSWS_ERROR_RECV_FAILED = -6,
    OBSWS_ERROR_SSL_FAILED = -11,
    
    /* Authentication errors (recoverable only by fixing the password) */
    OBSWS_ERROR_AUTH_FAILED = -3,
    
    /* Protocol/messaging errors (typically indicate bad request data or OBS issues) */
    OBSWS_ERROR_PARSE_FAILED = -7,
    OBSWS_ERROR_NOT_CONNECTED = -8,
    OBSWS_ERROR_ALREADY_CONNECTED = -9,
    
    /* Timeout errors (recoverable by retrying with patience) */
    OBSWS_ERROR_TIMEOUT = -4,
    
    /* System resource errors (usually indicates system-wide issues) */
    OBSWS_ERROR_OUT_OF_MEMORY = -10,
    
    /* Catch-all for things we didn't expect */
    OBSWS_ERROR_UNKNOWN = -99
} obsws_error_t;

/**
 * Connection state - represents the current phase of the connection lifecycle.
 * 
 * The connection goes through several states as it initializes. Understanding these
 * states is important because different operations are only valid in certain states.
 * For example, you can't send scene-switching commands when the state is CONNECTING
 * - you have to wait until CONNECTED.
 * 
 * The state machine looks like this:
 *   DISCONNECTED -> CONNECTING -> AUTHENTICATING -> CONNECTED
 *   Any state can transition to ERROR if something goes wrong
 *   CONNECTED or ERROR can go back to DISCONNECTED when closing
 * 
 * When you get a state callback, it tells you the old and new states so you can
 * react appropriately. For example, you might want to disable UI buttons when
 * moving from CONNECTED to DISCONNECTED.
 */
typedef enum {
    OBSWS_STATE_DISCONNECTED = 0,       /* Not connected to OBS, no operations possible */
    OBSWS_STATE_CONNECTING = 1,         /* WebSocket handshake in progress, wait for AUTHENTICATING */
    OBSWS_STATE_AUTHENTICATING = 2,     /* Connected but still doing auth, wait for CONNECTED */
    OBSWS_STATE_CONNECTED = 3,          /* Ready - authentication complete, send commands now */
    OBSWS_STATE_ERROR = 4               /* Unrecoverable error occurred, reconnection might help */
} obsws_state_t;

/**
 * Log levels for filtering library output.
 * 
 * Think of log levels like a funnel - higher levels include all the output from
 * lower levels plus more. So LOG_DEBUG includes everything, while LOG_ERROR only
 * shows when things go wrong.
 * 
 * For production, use OBSWS_LOG_ERROR or OBSWS_LOG_WARNING to avoid spam.
 * For development/debugging, use OBSWS_LOG_DEBUG to see everything happening.
 */
typedef enum {
    OBSWS_LOG_NONE = 0,         /* Silence the library completely */
    OBSWS_LOG_ERROR = 1,        /* Only errors - something went wrong */
    OBSWS_LOG_WARNING = 2,      /* Errors + warnings - potential issues but still working */
    OBSWS_LOG_INFO = 3,         /* Normal operation info, good for seeing what's happening */
    OBSWS_LOG_DEBUG = 4         /* Very verbose, includes internal decisions and state changes */
} obsws_log_level_t;

/**
 * Debug levels - fine-grained control for troubleshooting connection issues.
 * 
 * Debug output is separate from log output because it's meant for developers
 * debugging the library itself. It shows low-level protocol details. You probably
 * only need this if something seems broken or you're curious about the protocol.
 * 
 * WARNING: Debug level HIGH will log passwords and raw messages. Never use in
 * production or with untrusted users watching the output.
 */
typedef enum {
    OBSWS_DEBUG_NONE = 0,       /* No debug output - production mode */
    OBSWS_DEBUG_LOW = 1,        /* Connection events, auth success/failure, major state changes */
    OBSWS_DEBUG_MEDIUM = 2,     /* Low + WebSocket opcodes, event type names, request IDs */
    OBSWS_DEBUG_HIGH = 3        /* Medium + full message contents - can include passwords! */
} obsws_debug_level_t;

/* Forward declaration of connection handle - opaque structure for connection management */
typedef struct obsws_connection obsws_connection_t;

/**
 * Log callback function type - called when the library generates log messages.
 * 
 * This callback gives you a chance to handle logging however you want - write to a
 * file, display in a GUI, send to a remote server, etc. If you don't provide a log
 * callback, messages go to stderr by default.
 * 
 * @param level The severity level of this log message (error, warning, info, debug)
 * @param message The actual log message text (null-terminated string)
 * @param user_data Pointer you provided in the config - use for context
 * 
 * @note The message buffer is temporary and may be freed after this callback returns.
 *       If you need to keep the message, copy it with strdup() or similar.
 * @note This callback is called from an internal thread, so if you access shared
 *       data structures, protect them with mutexes.
 * @note Avoid doing expensive operations in this callback - logging should be fast.
 */
typedef void (*obsws_log_callback_t)(obsws_log_level_t level, const char *message, void *user_data);

/**
 * Event callback function type - called when OBS sends an event.
 * 
 * OBS sends events to tell you about things happening - scene changed, recording
 * started, input muted, etc. Events come as JSON in event_data. You have to parse
 * it yourself using something like cJSON. We don't parse it for you because different
 * applications care about different events, so we save CPU by leaving parsing to you.
 * 
 * @param conn The connection this event came from (useful if you have multiple OBS instances)
 * @param event_type String name of the event like "SceneChanged", "RecordingStateChanged"
 * @param event_data JSON string with event details - parse this yourself with cJSON or json-c
 * @param user_data Pointer you provided in the config
 * 
 * @note The event_data buffer is temporary and freed after this callback returns.
 *       If you need to keep the data, copy the string or parse it immediately.
 * @note This callback is called from an internal thread, so synchronize access to
 *       shared data structures.
 * @note Don't block or do expensive work in this callback - events could pile up.
 * 
 * @example Parsing an event might look like:
 *   cJSON *event_obj = cJSON_Parse(event_data);
 *   if (event_obj && cJSON_HasObjectItem(event_obj, "eventData")) {
 *       cJSON *data = cJSON_GetObjectItem(event_obj, "eventData");
 *       // Now examine event_obj to see what changed
 *   }
 *   if (event_obj) cJSON_Delete(event_obj);
 */
typedef void (*obsws_event_callback_t)(obsws_connection_t *conn, const char *event_type, const char *event_data, void *user_data);

/**
 * State callback function type - called when connection state changes.
 * 
 * This is how you know when the connection comes up or goes down. Use this to
 * update your UI - disable buttons when disconnected, enable them when connected,
 * show spinners during connecting, etc.
 * 
 * The callback receives both the old and new states so you can see the transition.
 * For example, if old_state is DISCONNECTED and new_state is CONNECTING, you might
 * show a "connecting..." message. If old_state is CONNECTED and new_state is
 * DISCONNECTED, you might show "disconnected" and disable sending commands.
 * 
 * @param conn The connection whose state changed
 * @param old_state Previous state (what it was before)
 * @param new_state Current state (what it is now)
 * @param user_data Pointer you provided in the config
 * 
 * @note This callback is called from an internal thread, so protect shared data.
 * @note Don't do slow operations here - state changes should be handled quickly.
 * 
 * State transitions:
 * - DISCONNECTED -> CONNECTING (connection attempt starting)
 * - CONNECTING -> AUTHENTICATING (WebSocket connected, checking auth)
 * - AUTHENTICATING -> CONNECTED (ready to use)
 * - CONNECTING -> ERROR (connection failed)
 * - AUTHENTICATING -> ERROR (auth failed)
 * - CONNECTED -> DISCONNECTED (user disconnected or connection lost)
 * - CONNECTED -> ERROR (unexpected connection drop)
 * - ERROR -> DISCONNECTED (after cleanup)
 * - Any state -> DISCONNECTED (when you call obsws_disconnect)
 */
typedef void (*obsws_state_callback_t)(obsws_connection_t *conn, obsws_state_t old_state, obsws_state_t new_state, void *user_data);

/**
 * Connection configuration structure.
 * 
 * This structure holds all the settings for connecting to OBS. You should fill this
 * out with your specific needs, then pass it to obsws_connect(). A good starting point
 * is to call obsws_config_init() which fills it with reasonable defaults, then only
 * change the fields you care about (usually just host, port, and password).
 * 
 * Design note: We use a config struct instead of many function parameters because
 * it's more flexible - adding new configuration options doesn't break existing code.
 * It also makes it clear what options are available.
 */
typedef struct {
    /* === Connection Parameters === */
    const char *host;                    /* IP or hostname of OBS (e.g., "192.168.1.100" or "obs.example.com") */
    uint16_t port;                       /* OBS WebSocket server port, usually 4455, sometimes 4454 for WSS */
    const char *password;                /* OBS WebSocket password from settings. Set to NULL for no auth. */
    bool use_ssl;                        /* Use WSS (WebSocket Secure) instead of WS. Requires OBS configured for SSL. */
    
    /* === Timeout Settings (all in milliseconds) ===
       Timeouts are important to prevent hanging. Too short and you get false failures.
       Too long and your app freezes. Adjust based on network quality. */
    uint32_t connect_timeout_ms;         /* How long to wait for initial TCP connection (default: 5000) */
    uint32_t recv_timeout_ms;            /* How long to wait for data from OBS (default: 5000) */
    uint32_t send_timeout_ms;            /* How long to wait to send data to OBS (default: 5000) */
    
    /* === Keep-Alive / Health Monitoring ===
       The library sends ping messages periodically to detect dead connections.
       If OBS stops responding to pings, the library will try to reconnect. */
    uint32_t ping_interval_ms;           /* Send ping this often (default: 10000, 0 to disable pings) */
    uint32_t ping_timeout_ms;            /* Wait this long for pong response (default: 5000) */
    
    /* === Automatic Reconnection ===
       If the connection dies, should we try to reconnect? Very useful for production
       because networks hiccup, OBS crashes, etc. The library uses exponential backoff
       to avoid hammering the server - delays double each attempt up to the max. */
    bool auto_reconnect;                 /* Enable automatic reconnection (default: true) */
    uint32_t reconnect_delay_ms;         /* Wait this long before first reconnect (default: 1000) */
    uint32_t max_reconnect_delay_ms;     /* Don't wait longer than this between attempts (default: 30000) */
    uint32_t max_reconnect_attempts;     /* Give up after this many attempts (0 = retry forever) */
    
    /* === Callbacks ===
       These optional callbacks let you be notified of important events.
       You can leave any of them NULL if you don't care about that event type. */
    obsws_log_callback_t log_callback;   /* Called when library logs something */
    obsws_event_callback_t event_callback;  /* Called when OBS sends an event */
    obsws_state_callback_t state_callback;  /* Called when connection state changes */
    void *user_data;                     /* Passed to all callbacks - use for context (like "this" pointer) */
} obsws_config_t;

/**
 * Connection statistics - useful for monitoring and debugging connection health.
 * 
 * These stats let you see what's happening on the connection - how many messages
 * have been sent/received, error counts, latency, etc. Useful for monitoring the
 * connection quality, detecting if something is wrong, or just being curious about
 * protocol activity. You get these by calling obsws_get_stats().
 */
typedef struct {
    uint64_t messages_sent;              /* Total WebSocket messages sent to OBS (includes ping/pong) */
    uint64_t messages_received;          /* Total WebSocket messages received from OBS (includes events) */
    uint64_t bytes_sent;                 /* Total bytes transmitted, useful for bandwidth monitoring */
    uint64_t bytes_received;             /* Total bytes received */
    uint64_t reconnect_count;            /* How many times auto-reconnect kicked in (0 if never disconnected) */
    uint64_t error_count;                /* Total errors encountered (some might be retried successfully) */
    uint64_t last_ping_ms;               /* Round-trip time of last ping - network latency indicator */
    time_t connected_since;              /* Unix timestamp of when this connection was established */
} obsws_stats_t;

/**
 * Response structure for requests to OBS.
 * 
 * When you send a request like obsws_set_current_scene(), you can get back a
 * response with the result. The response tells you if it succeeded, and if not,
 * why it failed. It might also contain response data from OBS - for example,
 * obsws_get_current_scene() puts the scene name in response_data as JSON.
 * 
 * If you don't care about the response, you can pass NULL and not get one back.
 * Otherwise you must free it with obsws_response_free() when done.
 * 
 * Design note: Responses are returned as strings instead of parsed JSON to save
 * CPU - different callers care about different response fields, so we let them
 * parse what they need. This also avoids dependency bloat.
 */
typedef struct {
    bool success;                        /* true if OBS said the operation worked */
    int status_code;                     /* OBS status code: 100-199 = success, 600+ = error */
    char *error_message;                 /* If success is false, this has the reason (e.g., "Scene does not exist") */
    char *response_data;                 /* Raw JSON response from OBS - parse yourself with cJSON */
} obsws_response_t;

/* ============================================================================
 * Library Initialization and Cleanup
 * ============================================================================ */

/**
 * Initialize the OBS WebSocket library.
 * 
 * This must be called once, before using any other library functions. It sets up
 * global state like threading primitives and initializes dependencies like
 * libwebsockets and OpenSSL. If you call it multiple times, subsequent calls are
 * ignored (thread-safe).
 * 
 * Typical usage:
 *   if (obsws_init() != OBSWS_OK) {
 *       fprintf(stderr, "Failed to init library\n");
 *       return 1;
 *   }
 *   // ... use the library ...
 *   obsws_cleanup();
 * 
 * @return OBSWS_OK on success, error code if initialization failed
 * 
 * @note Call this from your main thread before spawning other threads.
 * @note Very thread-safe - can be called multiple times, only initializes once.
 */
obsws_error_t obsws_init(void);

/**
 * Cleanup the OBS WebSocket library.
 * 
 * Call this when done using the library to release resources. Any connections
 * should be disconnected before cleanup, though the library will try to clean
 * them up if they're not. After calling this, don't use any library functions
 * until you call obsws_init() again.
 * 
 * @note It's safe to call this multiple times.
 * @note You should obsws_disconnect() all connections before calling this.
 * @note Threads should exit before calling cleanup - don't cleanup while
 *       callbacks are running.
 */
void obsws_cleanup(void);

/**
 * Get the library version string.
 * 
 * Returns a string like "1.0.0" indicating what version of the library you're
 * using. Useful for debugging - you can log this at startup so you know which
 * version of the library handled a particular issue.
 * 
 * @return Pointer to version string (valid until obsws_cleanup is called)
 */
const char* obsws_version(void);

/**
 * Set the global log level for the library.
 * 
 * This affects all library output - messages at this level and below are shown.
 * For example, if you set OBSWS_LOG_WARNING, you'll see warnings and errors but
 * not info or debug messages. This setting affects all connections.
 * 
 * Common usage:
 *   - Development: OBSWS_LOG_DEBUG - see everything while developing
 *   - Testing: OBSWS_LOG_INFO - see what's happening
 *   - Production: OBSWS_LOG_ERROR or OBSWS_LOG_WARNING - only see problems
 * 
 * @param level Minimum log level to output
 */
void obsws_set_log_level(obsws_log_level_t level);

/**
 * Set the global debug level for the library.
 * 
 * This controls detailed internal logging separate from regular log messages.
 * Use this for troubleshooting connection/protocol issues. Each level includes
 * all output from lower levels.
 * 
 * Debug output shows protocol-level information - opcodes, message IDs, etc.
 * It's verbose and should only be used during development.
 * 
 * WARNING: Level HIGH logs passwords and raw messages. Never use in production
 * or with untrusted users watching the output.
 * 
 * @param level Debug verbosity level
 */
void obsws_set_debug_level(obsws_debug_level_t level);

/**
 * Get the current global debug level.
 * 
 * Returns what the debug level is set to. Useful if you have code that needs to
 * know how verbose the debugging is.
 * 
 * @return Current debug level
 */
obsws_debug_level_t obsws_get_debug_level(void);

/* ============================================================================
 * Advanced Logging System - File Logging, Timestamps, Colors, Rotation
 * ============================================================================ */

/**
 * Enable file logging to the specified directory.
 * 
 * When enabled, the library will write all log messages to timestamped log files
 * in the specified directory. Files are rotated daily at midnight (configurable).
 * If the directory doesn't exist, it will be created with restrictive permissions
 * (0700 for security). This is safe for multi-threaded use.
 * 
 * For Linux/Unix systems, if directory is NULL, defaults to ~/.config/libwsv5/logs
 * 
 * Example:
 *   // Enable logging to default location
 *   obsws_enable_log_file(NULL);
 *   
 *   // Or specify custom location
 *   obsws_enable_log_file("/var/log/myapp");
 * 
 * @param directory Directory path where log files should be written, or NULL for default
 * @return OBSWS_OK on success, error code if directory creation failed or permission denied
 * 
 * @note Thread-safe - can be called from any thread
 * @note Should be called before obsws_connect() for best results
 * @note Log files are created with format: libwsv5_YYYY-MM-DD.log
 */
obsws_error_t obsws_enable_log_file(const char *directory);

/**
 * Disable file logging.
 * 
 * Stops writing log messages to files. In-flight logs are flushed first.
 * Does nothing if file logging was never enabled.
 * 
 * @return OBSWS_OK always
 * 
 * @note Thread-safe
 */
obsws_error_t obsws_disable_log_file(void);

/**
 * Configure daily log file rotation.
 * 
 * By default, log files rotate at midnight UTC. You can change this to a
 * different hour (0-23) or disable rotation with hour = -1.
 * 
 * When a rotation occurs, the current log file is renamed with the date
 * (e.g., libwsv5_2024-03-15.log) and a new file is started.
 * 
 * @param hour Hour of day (0-23) when rotation should occur, or -1 to disable
 * @return OBSWS_OK on success, OBSWS_ERROR_INVALID_PARAM if hour is invalid
 * 
 * @note Thread-safe
 */
obsws_error_t obsws_set_log_rotation_hour(int hour);

/**
 * Configure log file rotation by size.
 * 
 * If set to a non-zero value, log files will also rotate when they exceed
 * this size in bytes. When rotation occurs, the file is renamed with a
 * timestamp (e.g., libwsv5_2024-03-15_14-30-45.log) and a new file is started.
 * 
 * Set to 0 to disable size-based rotation (default).
 * Common values: 1048576 (1MB), 10485760 (10MB), 104857600 (100MB)
 * 
 * @param max_size_bytes Maximum size in bytes before rotation, or 0 to disable
 * @return OBSWS_OK always
 * 
 * @note Thread-safe
 * @note Size check happens during each log write, so actual file might be
 *       slightly larger than max_size_bytes
 */
obsws_error_t obsws_set_log_rotation_size(size_t max_size_bytes);

/**
 * Configure ANSI color output for console logs.
 * 
 * Controls whether colored output is used when writing to console/stderr.
 * Colors help visually distinguish error (red), warning (yellow), info (green),
 * and debug messages.
 * 
 * Mode values:
 * - 0: Force colors OFF (even on TTY)
 * - 1: Force colors ON (even if not a TTY)
 * - 2: Auto-detect (ON if output is a TTY, OFF otherwise) - DEFAULT
 * 
 * @param mode Color mode (0=force_off, 1=force_on, 2=auto_detect)
 * @return OBSWS_OK on success, OBSWS_ERROR_INVALID_PARAM if mode is invalid
 * 
 * @note Thread-safe
 * @note Colors only apply to console output, not file logs
 */
obsws_error_t obsws_set_log_colors(int mode);

/**
 * Enable or disable timestamps in log output.
 * 
 * When enabled (default), each log message is prefixed with a timestamp
 * in format [YYYY-MM-DD HH:MM:SS.mmm]. This makes it easy to correlate
 * library events with other system events.
 * 
 * @param enabled true to include timestamps, false to omit them
 * @return OBSWS_OK always
 * 
 * @note Thread-safe
 * @note Affects both file and console output
 */
obsws_error_t obsws_set_log_timestamps(bool enabled);

/**
 * Get the current log file directory.
 * 
 * Returns the directory where log files are being written, or NULL if
 * file logging is disabled. The returned pointer is valid until the next
 * call to obsws_enable_log_file() or obsws_cleanup().
 * 
 * @return Pointer to log directory string, or NULL if disabled
 * 
 * @note NOT thread-safe - call only from main thread or with synchronization
 */
const char* obsws_get_log_file_directory(void);

/* ============================================================================
 * Connection Management
 * ============================================================================ */

/**
 * Create a default configuration structure with reasonable defaults.
 * 
 * This initializes a config structure with settings that should work for most
 * cases. You then modify only the fields you care about. This is better than
 * manually setting each field because if we add new config options in the future,
 * you'll automatically get the right defaults without changing your code.
 * 
 * After calling this, typically you'd set:
 *   config.host = "192.168.1.100";
 *   config.port = 4455;
 *   config.password = "your-obs-password";
 * 
 * Then pass it to obsws_connect().
 * 
 * @param config Pointer to configuration structure to fill with defaults
 */
void obsws_config_init(obsws_config_t *config);

/**
 * Create a new OBS WebSocket connection and start connecting.
 * 
 * This function creates a connection object and begins the connection process
 * in the background. The connection goes through states: DISCONNECTED ->
 * CONNECTING -> AUTHENTICATING -> CONNECTED. You'll be notified of state
 * changes via the state callback (if you provided one in config).
 * 
 * This is non-blocking - it returns immediately. The actual connection happens
 * in a background thread. Check obsws_get_state() to see when it reaches CONNECTED.
 * 
 * Design note: We do connection in the background because it can take a while
 * (DNS lookups, TCP handshake, authentication). Blocking would freeze your app.
 * 
 * @param config Connection configuration (required, cannot be NULL)
 * @return Connection handle on success (never NULL if called with valid config),
 *         NULL on failure (usually out of memory)
 * 
 * @note The config structure is copied internally, so you can free or reuse it
 *       after calling obsws_connect().
 * @note The returned handle is opaque - use it with other obsws_* functions.
 * @note Connection attempt happens asynchronously - wait for state callback or
 *       call obsws_get_state() to check when it's ready.
 * @note If auto_reconnect is enabled in config, the library will automatically
 *       try to reconnect if the connection drops.
 * 
 * @example Usage:
 *   obsws_config_t config;
 *   obsws_config_init(&config);
 *   config.host = "192.168.1.100";
 *   config.port = 4455;
 *   config.password = "mypassword";
 *   config.state_callback = my_state_callback;
 *   config.user_data = my_app_context;
 *   
 *   obsws_connection_t *conn = obsws_connect(&config);
 *   if (!conn) {
 *       fprintf(stderr, "Failed to create connection\n");
 *       return 1;
 *   }
 *   // Wait for state callback to indicate CONNECTED...
 */
obsws_connection_t* obsws_connect(const obsws_config_t *config);

/**
 * Disconnect and destroy a connection.
 * 
 * This closes the connection to OBS and releases all associated resources.
 * After calling this, the connection handle is invalid - don't use it anymore.
 * 
 * If auto_reconnect was enabled, it stops trying to reconnect. If you want to
 * connect again, create a new connection with obsws_connect().
 * 
 * This function blocks until the background thread cleanly shuts down. It should
 * be fast (under 100ms), but avoid calling it from callbacks since callbacks
 * run in the connection thread - this would deadlock.
 * 
 * @param conn Connection handle to destroy (can be NULL, which does nothing)
 * 
 * @note Safe to call even if already disconnected.
 * @note Don't call from inside callbacks (they run in the connection thread).
 * @note After calling this, all pending requests are abandoned (responses never arrive).
 */
void obsws_disconnect(obsws_connection_t *conn);

/**
 * Check if connection is currently authenticated and ready to use.
 * 
 * Returns true only if the state is CONNECTED - meaning authentication completed
 * and you can send commands. Returns false in all other states (DISCONNECTED,
 * CONNECTING, AUTHENTICATING, ERROR).
 * 
 * This is the main function to check before sending commands. If it returns false,
 * commands will fail with OBSWS_ERROR_NOT_CONNECTED.
 * 
 * @param conn Connection handle
 * @return true if fully connected and authenticated, false otherwise
 * 
 * @note Fast operation - just checks internal state variable.
 * @note Prefer using state callbacks to be notified of changes instead of polling.
 */
bool obsws_is_connected(const obsws_connection_t *conn);

/**
 * Get the detailed current connection state.
 * 
 * Similar to obsws_is_connected() but returns the full state, not just a boolean.
 * This lets you distinguish between different states - for example, you might show
 * "connecting..." if the state is CONNECTING, vs "disconnected" if DISCONNECTED.
 * 
 * @param conn Connection handle
 * @return Current connection state (see obsws_state_t for the state machine)
 */
obsws_state_t obsws_get_state(const obsws_connection_t *conn);

/**
 * Get connection statistics and performance metrics.
 * 
 * Returns counters showing what's happened on this connection - messages sent,
 * bytes transmitted, errors, reconnect attempts, etc. Useful for monitoring,
 * debugging, or just curiosity.
 * 
 * The latency field (last_ping_ms) shows the round-trip time of the last ping,
 * which indicates network quality. Higher values mean slower network or more
 * congestion.
 * 
 * @param conn Connection handle
 * @param stats Pointer to structure to fill (required)
 * @return OBSWS_OK on success, error code if conn is invalid
 * 
 * @note fast operation - just copies the stats struct.
 */
obsws_error_t obsws_get_stats(const obsws_connection_t *conn, obsws_stats_t *stats);

/**
 * Manually trigger a reconnection attempt.
 * 
 * Normally the library handles reconnection automatically if auto_reconnect is
 * enabled. This function lets you force a reconnect attempt right now, for example
 * if you detect the connection seems dead even though the library hasn't noticed yet.
 * 
 * If the connection is currently connected, this disconnects and reconnects.
 * If it's not connected, this starts a connection attempt.
 * 
 * @param conn Connection handle
 * @return OBSWS_OK if reconnection started, error code otherwise
 * 
 * @note This is async - it returns immediately, reconnection happens in background.
 * @note If auto_reconnect is disabled, this still reconnects one time.
 */
obsws_error_t obsws_reconnect(obsws_connection_t *conn);

/**
 * Send a ping and measure round-trip time to check connection health.
 * 
 * Sends a WebSocket ping to OBS and waits for the pong response. The round-trip
 * time tells you the network latency. If the ping times out, it indicates a
 * problem - either the network is very slow or OBS is not responding.
 * 
 * This is useful for:
 *   - Checking if the connection is alive
 *   - Measuring network latency
 *   - Detecting when a connection appears OK but OBS isn't responding
 * 
 * @param conn Connection handle
 * @param timeout_ms How long to wait for pong (milliseconds). Use 5000-10000 as typical.
 * @return Round-trip time in milliseconds if successful, negative error code if it failed
 *         (probably OBSWS_ERROR_TIMEOUT if OBS isn't responding)
 * 
 * @note Returns immediately if not connected - doesn't attempt to connect.
 * @note The library also sends pings automatically at ping_interval_ms, so you
 *       usually don't need to call this manually.
 * @note A high latency (e.g., several seconds) might indicate network problems.
 */
int obsws_ping(obsws_connection_t *conn, uint32_t timeout_ms);

/* ============================================================================
 * Scene Management
 * ============================================================================ */

/**
 * Switch to a specific scene in OBS.
 * 
 * This tells OBS to make a different scene active. When you switch scenes, all
 * sources in the new scene become visible (if their visibility is on), and sources
 * from the old scene disappear. This is the main way to change what's being streamed
 * or recorded.
 * 
 * Switching scenes is fast but not instant - OBS processes the request and sends
 * a SceneChanged event when complete. If you have many sources with animations,
 * the transition might take a second or so.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param scene_name Name of the scene to activate (must exist in OBS, case-sensitive)
 * @param response Optional pointer to receive response. If you pass a pointer to
 *        response pointer, you get back a response object that you must free with
 *        obsws_response_free(). Pass NULL if you don't care about the response.
 * @return OBSWS_OK if the request was sent successfully (doesn't mean OBS processed it yet)
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if scene_name is NULL
 * 
 * @note The scene must exist in OBS. If you try to switch to a non-existent scene,
 *       OBS will return an error in the response.
 * @note This is async - the function returns before OBS actually switches scenes.
 *       Watch for a SceneChanged event to know when it completed.
 * @note If you only care about success/failure, you can pass NULL for response.
 * @note The response contains OBS status codes - 100-199 is success, 600+ is error.
 * 
 * @example Switching to a scene:
 *   obsws_response_t *response = NULL;
 *   obsws_error_t err = obsws_set_current_scene(conn, "Gaming Scene", &response);
 *   if (err == OBSWS_OK && response) {
 *       if (response->success) {
 *           printf("Scene switched successfully\n");
 *       } else {
 *           printf("OBS error: %s\n", response->error_message);
 *       }
 *       obsws_response_free(response);
 *   }
 */
obsws_error_t obsws_set_current_scene(obsws_connection_t *conn, const char *scene_name, obsws_response_t **response);

/**
 * Get the name of the currently active scene.
 * 
 * Asks OBS which scene is currently shown. The answer comes back as a string that
 * you provide a buffer for. If the buffer is too small, the function fails with
 * OBSWS_ERROR_RECV_FAILED (or similar) because the response doesn't fit.
 * 
 * This is useful to check what scene is active before switching, or to sync your
 * UI state with OBS (in case OBS was controlled by something else).
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param scene_name Output buffer where the scene name will be written
 * @param buffer_size Size of the buffer (how many bytes can fit)
 * @return OBSWS_OK on success
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if scene_name is NULL
 * 
 * @note The buffer should be large enough for typical scene names. OBS doesn't
 *       limit scene name length, but practically they're usually under 256 characters.
 *       256 or 512 bytes is usually safe.
 * @note The string written to scene_name is null-terminated.
 * @note This is synchronous - it waits for response before returning (blocking).
 * 
 * @example Getting current scene:
 *   char current_scene[256];
 *   if (obsws_get_current_scene(conn, current_scene, sizeof(current_scene)) == OBSWS_OK) {
 *       printf("Current scene: %s\n", current_scene);
 *   }
 */
obsws_error_t obsws_get_current_scene(obsws_connection_t *conn, char *scene_name, size_t buffer_size);

/**
 * Get a list of all available scenes in the OBS session.
 * 
 * Asks OBS for the list of all scenes it knows about. Returns an array of scene
 * name strings. You're responsible for freeing both the individual strings and
 * the array itself using obsws_free_scene_list().
 * 
 * Useful for:
 *   - Building a scene switcher UI
 *   - Validating that a scene name exists before trying to switch to it
 *   - Showing what scenes are available
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param scenes Output parameter - receives pointer to array of scene name strings.
 *        Each string is allocated with malloc and null-terminated. The array itself
 *        is also malloc'd. You must free with obsws_free_scene_list().
 * @param count Output parameter - receives the number of scenes in the array
 * @return OBSWS_OK on success
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if scenes or count is NULL
 * 
 * @note The returned array is heap-allocated. Always free it with obsws_free_scene_list()
 *       when done, or you leak memory. Don't use free() or free the individual
 *       strings manually - use the provided function.
 * @note This is synchronous - waits for response.
 * @note If OBS has no scenes (unusual), you get count=0 and scenes=pointer to empty array.
 * 
 * @example Getting and using scene list:
 *   char **scenes = NULL;
 *   size_t count = 0;
 *   if (obsws_get_scene_list(conn, &scenes, &count) == OBSWS_OK) {
 *       printf("Available scenes: %zu\n", count);
 *       for (size_t i = 0; i < count; i++) {
 *           printf("  - %s\n", scenes[i]);
 *       }
 *       obsws_free_scene_list(scenes, count);
 *   }
 */
obsws_error_t obsws_get_scene_list(obsws_connection_t *conn, char ***scenes, size_t *count);

/**
 * Switch to a different scene collection.
 * 
 * Scene collections are different sets of scenes. OBS can have multiple scene
 * collections and you can switch between them. When you switch collections, all
 * the scenes in the current collection are replaced with the scenes from the new
 * collection. This is useful for different contexts (e.g., "Gaming", "IRL",
 * "Voiceover", etc.).
 * 
 * Switching collections can take a moment because OBS needs to load all the new
 * scenes and their settings. You'll get a SceneCollectionChanged event when done.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param collection_name Name of the scene collection to activate (case-sensitive,
 *        must exist in OBS)
 * @param response Optional pointer to receive response (NULL to ignore)
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if collection_name is NULL
 * 
 * @note Scene collection names are shown in OBS: Scene Collection dropdown.
 * @note Switching collections is slower than switching scenes (requires loading files).
 * @note This is async - function returns before OBS finishes switching.
 * @note If the collection doesn't exist, OBS returns an error in the response.
 */
obsws_error_t obsws_set_scene_collection(obsws_connection_t *conn, const char *collection_name, obsws_response_t **response);

/* ============================================================================
 * Recording and Streaming Control
 * ============================================================================ */

/**
 * Start recording to disk.
 * 
 * Tells OBS to begin recording the current scene and audio to the configured
 * output file (usually somewhere in your Videos folder). This is separate from
 * streaming - you can record without streaming, stream without recording, or do both.
 * 
 * The actual file path is configured in OBS settings - this library doesn't control
 * where the file goes or what format it uses. Those are OBS preferences.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully (doesn't mean recording started yet)
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * 
 * @note This is async - function returns immediately, recording starts in background.
 * @note You'll get a RecordingStateChanged event when recording actually starts.
 * @note If recording is already running, OBS ignores this request (no error).
 * @note The recorded file format depends on OBS configuration (usually MP4 or MKV).
 * 
 * @example Starting to record:
 *   if (obsws_start_recording(conn, NULL) == OBSWS_OK) {
 *       printf("Recording start request sent\n");
 *   }
 */
obsws_error_t obsws_start_recording(obsws_connection_t *conn, obsws_response_t **response);

/**
 * Stop recording.
 * 
 * Tells OBS to stop recording and save the file. The saved recording will include
 * everything from when you called obsws_start_recording() until now. If recording
 * wasn't running, OBS ignores this (no error).
 * 
 * After calling this, OBS takes a moment to finalize the file (write headers, etc.),
 * then sends a RecordingStateChanged event when complete. You can't immediately
 * open the file - wait for the event first.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * 
 * @note This is async - the file finalization happens after the function returns.
 * @note OBS sends a RecordingStateChanged event when the file is ready.
 * @note Don't try to move/open the file immediately - wait for the event first.
 */
obsws_error_t obsws_stop_recording(obsws_connection_t *conn, obsws_response_t **response);

/**
 * Start streaming to a remote server.
 * 
 * Tells OBS to begin streaming the current scene and audio to the configured
 * streaming service (Twitch, YouTube, etc.). The stream settings (server URL, key,
 * bitrate, resolution, etc.) are all configured in OBS - this library just tells
 * OBS when to start and stop.
 * 
 * Streaming uses the same video/audio sources as recording, but you can have
 * different settings (OBS can transcode differently for streaming vs recording).
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * 
 * @note This is async - streaming starts in background.
 * @note You'll get a StreamStateChanged event when streaming actually starts.
 * @note Streaming won't start if the streaming settings are invalid (wrong RTMP key, etc).
 * @note If already streaming, OBS ignores this request.
 * @note Network conditions determine whether streaming succeeds - bad internet = failure.
 * 
 * @example Starting a stream:
 *   obsws_error_t err = obsws_start_streaming(conn, NULL);
 *   if (err == OBSWS_OK) {
 *       // Wait for StreamStateChanged event to confirm it started
 *   }
 */
obsws_error_t obsws_start_streaming(obsws_connection_t *conn, obsws_response_t **response);

/**
 * Stop streaming.
 * 
 * Tells OBS to stop streaming to the remote server. After calling this, OBS
 * disconnects from the streaming server and the stream ends. The stopped event
 * will tell you if the disconnect was clean or if there was an issue.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * 
 * @note This is async - disconnecting from the server takes a moment.
 * @note OBS sends a StreamStateChanged event when streaming fully stops.
 * @note If not streaming, OBS ignores this request (no error).
 */
obsws_error_t obsws_stop_streaming(obsws_connection_t *conn, obsws_response_t **response);

/**
 * Get whether OBS is currently streaming.
 * 
 * Returns true if a stream is active (connected to the streaming server), false
 * otherwise. The response parameter (if provided) has more detailed info like
 * bandwidth, frames rendered, etc.
 * 
 * This is useful to check state after startup - maybe OBS was already streaming
 * before your app connected, and you want to know about it.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param is_streaming Output parameter - true if streaming, false if not
 * @param response Optional pointer to receive full response with stats
 * @return OBSWS_OK on success
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if is_streaming is NULL
 * 
 * @note is_streaming must be non-NULL, but response can be NULL.
 * @note Fast operation - just queries current state.
 * 
 * @example Checking if streaming:
 *   bool streaming = false;
 *   if (obsws_get_streaming_status(conn, &streaming, NULL) == OBSWS_OK) {
 *       printf("Streaming: %s\n", streaming ? "yes" : "no");
 *   }
 */
obsws_error_t obsws_get_streaming_status(obsws_connection_t *conn, bool *is_streaming, obsws_response_t **response);

/**
 * Get whether OBS is currently recording.
 * 
 * Returns true if recording is active, false otherwise. Similar to
 * obsws_get_streaming_status() but for recording instead.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param is_recording Output parameter - true if recording, false if not
 * @param response Optional pointer to receive full response with stats
 * @return OBSWS_OK on success
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if is_recording is NULL
 * 
 * @note is_recording must be non-NULL, but response can be NULL.
 */
obsws_error_t obsws_get_recording_status(obsws_connection_t *conn, bool *is_recording, obsws_response_t **response);

/* ============================================================================
 * Source Control
 * ============================================================================ */

/**
 * Set whether a source is visible in a scene.
 * 
 * Sources are the building blocks of scenes - they can be cameras, images, text,
 * browser windows, etc. Each source appears in one or more scenes, and you can
 * control whether it's shown or hidden. When you hide a source, it doesn't render
 * on the stream/recording until you show it again.
 * 
 * This is useful for:
 *   - Show/hide a watermark or banner
 *   - Toggle overlays on and off
 *   - Control which camera appears (if you have multiple cameras as sources)
 * 
 * Note: A source can exist in multiple scenes. Changing visibility in one scene
 * doesn't affect it in other scenes.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param scene_name Name of the scene containing the source (case-sensitive)
 * @param source_name Name of the source to hide/show (case-sensitive)
 * @param visible true to show the source, false to hide it
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if scene_name or source_name is NULL
 * 
 * @note If the scene or source doesn't exist, OBS returns an error in response.
 * @note Changes are instant - the source appears/disappears on stream immediately.
 * @note You'll get a SourceVisibilityChanged event when this completes.
 * 
 * @example Hiding a watermark source:
 *   obsws_set_source_visibility(conn, "Main Scene", "Watermark", false, NULL);
 */
obsws_error_t obsws_set_source_visibility(obsws_connection_t *conn, const char *scene_name, 
                                          const char *source_name, bool visible, obsws_response_t **response);

/**
 * Enable or disable a filter on a source.
 * 
 * Filters are effects applied to sources - color correction, blur, noise suppression,
 * etc. Each filter can be enabled or disabled. Disabling a filter removes its effect
 * without deleting it, so you can toggle it back on later.
 * 
 * Use this to:
 *   - Dynamically control effects (blur camera when not looking at it)
 *   - Toggle noise suppression on/off for a microphone source
 *   - Enable/disable color correction for different lighting conditions
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param source_name Name of the source containing the filter (case-sensitive)
 * @param filter_name Name of the filter to enable/disable (case-sensitive)
 * @param enabled true to enable the filter, false to disable it
 * @param response Optional pointer to receive response
 * @return OBSWS_OK if request sent successfully
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_INVALID_PARAM if source_name or filter_name is NULL
 * 
 * @note If the source or filter doesn't exist, OBS returns an error.
 * @note Changes are instant on stream/recording.
 * @note You'll get a SourceFilterEnabledStateChanged event when complete.
 * 
 * @example Disabling noise suppression:
 *   obsws_set_source_filter_enabled(conn, "Mic", "Noise Suppression", false, NULL);
 */
obsws_error_t obsws_set_source_filter_enabled(obsws_connection_t *conn, const char *source_name,
                                              const char *filter_name, bool enabled, obsws_response_t **response);

/* ============================================================================
 * Custom Requests
 * ============================================================================ */

/**
 * Send a custom request to OBS using the WebSocket protocol.
 * 
 * This is the escape hatch for accessing any OBS WebSocket v5 API that the library
 * doesn't have a convenience function for. You specify the request type (like
 * "GetVersion", "SetSourceName", etc.) and optionally provide request data as a
 * JSON string. The response comes back as JSON that you parse yourself.
 * 
 * The library handles all the protocol overhead - request IDs, timeouts, etc.
 * You just provide the high-level request type and parameters.
 * 
 * Why provide this instead of wrapping everything? Because the OBS API has many
 * functions, and we wanted to keep the library focused on the most common operations.
 * This gives you access to everything else without bloating the library.
 * 
 * @param conn Connection handle (must be in CONNECTED state)
 * @param request_type OBS request type name (e.g., "GetVersion", "SetSourceName")
 *        See OBS WebSocket v5 documentation for complete list
 * @param request_data JSON string with request parameters, or NULL if no parameters needed.
 *        For example: "{\"sceneName\": \"Main\", \"sourceName\": \"Camera\"}"
 * @param response Pointer to receive response object (must be freed with obsws_response_free)
 *        If you pass NULL, the request is still sent but you don't get a response back.
 * @param timeout_ms How long to wait for a response (milliseconds). 0 = use default timeout.
 * @return OBSWS_OK if request was sent and response received within timeout
 * @return OBSWS_ERROR_NOT_CONNECTED if not connected
 * @return OBSWS_ERROR_TIMEOUT if no response within timeout_ms
 * @return OBSWS_ERROR_INVALID_PARAM if request_type is NULL
 * @return OBSWS_ERROR_PARSE_FAILED if response JSON was malformed
 * 
 * @note You must know the OBS WebSocket v5 API to use this effectively. See
 *       https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md
 * @note The response_data field in the response contains the OBS response as a
 *       JSON string. You parse it with cJSON or similar.
 * @note If response is NULL, this becomes fire-and-forget - useful for commands
 *       where you don't care about the result.
 * @note Timeout of 0 uses the global receive timeout from config (usually 5000ms).
 * 
 * @example Getting OBS version:
 *   obsws_response_t *response = NULL;
 *   obsws_error_t err = obsws_send_request(conn, "GetVersion", NULL, &response, 5000);
 *   if (err == OBSWS_OK && response) {
 *       // Parse response->response_data as JSON to get version info
 *       cJSON *json = cJSON_Parse(response->response_data);
 *       // ... extract version fields ...
 *       cJSON_Delete(json);
 *       obsws_response_free(response);
 *   }
 * 
 * @example Setting a source name (with parameters):
 *   obsws_response_t *response = NULL;
 *   const char *params = "{\"sourceName\": \"OldName\", \"newName\": \"NewName\"}";
 *   obsws_send_request(conn, "SetSourceName", params, &response, 5000);
 *   if (response) {
 *       printf("Success: %d\n", response->success);
 *       obsws_response_free(response);
 *   }
 */
obsws_error_t obsws_send_request(obsws_connection_t *conn, const char *request_type, 
                                 const char *request_data, obsws_response_t **response, uint32_t timeout_ms);

/* ============================================================================
 * Event Handling
 * ============================================================================ */

/**
 * Process pending events from the WebSocket connection.
 * 
 * The library processes events in a background thread and calls your callbacks
 * as events arrive. This function is provided for API compatibility and for
 * applications that prefer to do event processing in the main loop rather than
 * in background threads.
 * 
 * In most cases, you don't need to call this - just set up your callbacks and
 * the library handles everything. Call this only if you want explicit control
 * over when event processing happens (e.g., in a game loop).
 * 
 * Note: Even if you don't call this function, events are still processed in the
 * background thread and callbacks are still called. This function is optional.
 * 
 * @param conn Connection handle
 * @param timeout_ms Maximum time to wait for events (milliseconds). 0 = return immediately
 *        without processing, non-zero = wait up to this long for events.
 * @return Number of events processed, or negative error code
 * 
 * @note This function is called automatically in the background thread, so you
 *       don't typically need to call it manually.
 * @note Callbacks are called from within this function (or from the background thread).
 * @note If you call this frequently with timeout_ms=0, you'll busy-wait (CPU usage).
 * @note This is provided mainly for API flexibility - most code should not use it.
 */
int obsws_process_events(obsws_connection_t *conn, uint32_t timeout_ms);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Free a response structure and all its allocated memory.
 * 
 * When you get a response from functions like obsws_set_current_scene() or
 * obsws_send_request(), you're responsible for freeing it when done. This
 * function frees the response_data and error_message strings, plus the response
 * struct itself.
 * 
 * Always call this when you're done with a response, or you leak memory. It's
 * safe to call with NULL (does nothing).
 * 
 * @param response Response to free (can be NULL)
 * 
 * @note Safe to call with NULL.
 * @note After calling this, don't access the response pointer anymore.
 * @note Don't call free() manually on responses - use this function.
 */
void obsws_response_free(obsws_response_t *response);

/**
 * Get a human-readable string for an error code.
 * 
 * Converts error codes like OBSWS_ERROR_AUTH_FAILED into strings like
 * "Authentication failed". Useful for logging and error messages. The returned
 * strings are static (not allocated) so don't free them.
 * 
 * @param error Error code to convert
 * @return Pointer to error string (e.g., "Invalid parameter", "Connection failed")
 *         Never returns NULL - unknown error codes get "Unknown error"
 * 
 * @note The returned string is static and valid for the lifetime of the program.
 * @note Don't free the returned pointer.
 * 
 * @example Logging an error:
 *   obsws_error_t err = obsws_set_current_scene(conn, "Scene", NULL);
 *   if (err != OBSWS_OK) {
 *       fprintf(stderr, "Error: %s\n", obsws_error_string(err));
 *   }
 */
const char* obsws_error_string(obsws_error_t error);

/**
 * Get a human-readable string for a connection state.
 * 
 * Converts connection state enums like OBSWS_STATE_CONNECTED into strings like
 * "Connected". Useful for debug output and status displays.
 * 
 * @param state Connection state to convert
 * @return Pointer to state string (e.g., "Disconnected", "Connecting", "Connected")
 *         Never returns NULL - unknown states get "Unknown"
 * 
 * @note The returned string is static, don't free it.
 * 
 * @example Displaying connection status:
 *   obsws_state_t state = obsws_get_state(conn);
 *   printf("Connection state: %s\n", obsws_state_string(state));
 */
const char* obsws_state_string(obsws_state_t state);

/**
 * Free a scene list array returned by obsws_get_scene_list().
 * 
 * When you call obsws_get_scene_list(), it allocates memory for the scene names
 * and the array itself. You must free all of this using this function when done.
 * Don't try to free the individual strings manually or use plain free() - that
 * won't work correctly.
 * 
 * Why have a special function for this instead of just free()? Because the memory
 * layout needs special handling - there's an array of pointers, each pointing to
 * separately allocated strings.
 * 
 * @param scenes Array of scene name strings (from obsws_get_scene_list)
 * @param count Number of scenes in the array
 * 
 * @note Safe to call with NULL scenes pointer (does nothing).
 * @note After calling this, the scenes pointer is invalid - don't use it anymore.
 * @note The count parameter must match what obsws_get_scene_list() returned.
 * 
 * @example Using and freeing scene list:
 *   char **scenes = NULL;
 *   size_t count = 0;
 *   if (obsws_get_scene_list(conn, &scenes, &count) == OBSWS_OK) {
 *       for (size_t i = 0; i < count; i++) {
 *           printf("Scene: %s\n", scenes[i]);
 *       }
 *       obsws_free_scene_list(scenes, count);  // Must do this!
 *   }
 */
void obsws_free_scene_list(char **scenes, size_t count);

#ifdef __cplusplus
}
#endif

#endif // LIBWSV5_LIBRARY_H
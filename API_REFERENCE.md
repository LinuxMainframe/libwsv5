# libwsv5 API Reference

Complete documentation of all libwsv5 functions, types, and structures.

---

## Table of Contents

1. [Initialization & Cleanup](#initialization--cleanup)
2. [Configuration](#configuration)
3. [Connection Management](#connection-management)
4. [Scene Operations](#scene-operations)
5. [Recording & Streaming](#recording--streaming)
6. [Source Control](#source-control)
7. [Generic Requests](#generic-requests)
8. [Status & Monitoring](#status--monitoring)
9. [Types & Structures](#types--structures)
10. [Error Handling](#error-handling)
11. [Callbacks](#callbacks)
12. [Constants](#constants)

---

## Initialization & Cleanup

### obsws_init()

Initialize the libwsv5 library.

**Signature:**
```c
obsws_error_t obsws_init(void);
```

**Returns:**
- `OBSWS_OK` - Library initialized successfully
- `OBSWS_ERROR_OUT_OF_MEMORY` - Memory allocation failed

**Description:**
Must be called once before creating any connections. Sets up internal data structures and resources.

**Example:**
```c
obsws_error_t err = obsws_init();
if (err != OBSWS_OK) {
    fprintf(stderr, "Failed to initialize: %s\n", obsws_error_string(err));
    return 1;
}
```

### obsws_cleanup()

Clean up library resources.

**Signature:**
```c
void obsws_cleanup(void);
```

**Description:**
Frees all library resources. Call after all connections are closed. Safe to call multiple times.

**Example:**
```c
obsws_disconnect(conn);
obsws_cleanup();
```

---

## Configuration

### obsws_config_init()

Initialize a configuration structure with default values.

**Signature:**
```c
void obsws_config_init(obsws_config_t *config);
```

**Parameters:**
- `config` - Pointer to configuration structure

**Description:**
Sets all configuration fields to sensible defaults. Must be called before setting custom values.

**Example:**
```c
obsws_config_t config;
obsws_config_init(&config);
config.host = "192.168.1.100";
config.port = 4455;
config.password = "mypassword";
```

### obsws_set_log_level()

Set the logging verbosity level.

**Signature:**
```c
void obsws_set_log_level(obsws_log_level_t level);
```

**Parameters:**
- `level` - Log level (OBSWS_LOG_ERROR, OBSWS_LOG_WARNING, OBSWS_LOG_INFO, OBSWS_LOG_DEBUG)

**Description:**
Controls which log messages are printed. Call before connecting for best results.

### obsws_set_debug_level()

Set the debug output level.

**Signature:**
```c
void obsws_set_debug_level(obsws_debug_level_t level);
```

**Parameters:**
- `level` - Debug level (OBSWS_DEBUG_NONE, OBSWS_DEBUG_LOW, OBSWS_DEBUG_MEDIUM, OBSWS_DEBUG_HIGH)

**Description:**
Higher levels output more detailed protocol information, useful for debugging.

### obsws_get_debug_level()

Get current debug level.

**Signature:**
```c
obsws_debug_level_t obsws_get_debug_level(void);
```

**Returns:**
- Current debug level

### obsws_set_log_timestamps()

Enable/disable timestamps in log output.

**Signature:**
```c
obsws_error_t obsws_set_log_timestamps(bool enabled);
```

**Parameters:**
- `enabled` - true to enable timestamps, false to disable

**Returns:**
- `OBSWS_OK` - Success

### obsws_set_log_colors()

Enable/disable colored output in logs.

**Signature:**
```c
obsws_error_t obsws_set_log_colors(int auto_detect);
```

**Parameters:**
- `auto_detect` - 0=disable, 1=force enable, 2=auto-detect terminal

**Returns:**
- `OBSWS_OK` - Success

---

## Connection Management

### obsws_connect()

Establish connection to OBS.

**Signature:**
```c
obsws_connection_t *obsws_connect(obsws_config_t *config);
```

**Parameters:**
- `config` - Connection configuration

**Returns:**
- Connection handle on success
- `NULL` on failure (check logs for details)

**Description:**
Creates a new connection to OBS and initiates WebSocket connection. Use `obsws_is_connected()` to verify connection is established.

**Example:**
```c
obsws_config_t config;
obsws_config_init(&config);
config.host = "localhost";
config.port = 4455;
config.password = "password";

obsws_connection_t *conn = obsws_connect(&config);
if (!conn) {
    fprintf(stderr, "Failed to connect\n");
    return 1;
}

// Wait for connection to establish
for (int i = 0; i < 100; i++) {
    if (obsws_is_connected(conn)) break;
    obsws_process_events(conn, 100);
}
```

### obsws_disconnect()

Close connection to OBS.

**Signature:**
```c
void obsws_disconnect(obsws_connection_t *conn);
```

**Parameters:**
- `conn` - Connection to close

**Description:**
Cleanly closes the WebSocket connection and frees resources. Safe to call multiple times.

**Example:**
```c
obsws_disconnect(conn);
```

### obsws_is_connected()

Check if connection is established and authenticated.

**Signature:**
```c
bool obsws_is_connected(obsws_connection_t *conn);
```

**Parameters:**
- `conn` - Connection to check

**Returns:**
- `true` if connected and authenticated
- `false` otherwise

**Example:**
```c
if (obsws_is_connected(conn)) {
    printf("Ready to send commands\n");
}
```

### obsws_get_state()

Get current connection state.

**Signature:**
```c
obsws_state_t obsws_get_state(obsws_connection_t *conn);
```

**Parameters:**
- `conn` - Connection to query

**Returns:**
- Current state (OBSWS_STATE_DISCONNECTED, OBSWS_STATE_CONNECTING, etc.)

### obsws_state_string()

Convert connection state to human-readable string.

**Signature:**
```c
const char *obsws_state_string(obsws_state_t state);
```

**Parameters:**
- `state` - State to convert

**Returns:**
- String representation (e.g., "Connected", "Disconnected")

**Example:**
```c
obsws_state_t state = obsws_get_state(conn);
printf("State: %s\n", obsws_state_string(state));
```

### obsws_ping()

Send keep-alive ping to OBS.

**Signature:**
```c
obsws_error_t obsws_ping(obsws_connection_t *conn);
```

**Parameters:**
- `conn` - Connection to ping

**Returns:**
- `OBSWS_OK` - Ping sent successfully
- Other error codes on failure

**Description:**
Manually sends a keep-alive ping. Usually not needed as library sends automatic pings.

---

## Scene Operations

### obsws_get_current_scene()

Get name of currently active scene.

**Signature:**
```c
obsws_error_t obsws_get_current_scene(obsws_connection_t *conn, char *scene_name, size_t name_size);
```

**Parameters:**
- `conn` - Connection
- `scene_name` - Buffer to receive scene name
- `name_size` - Buffer size

**Returns:**
- `OBSWS_OK` - Scene name retrieved
- `OBSWS_ERROR_NOT_CONNECTED` - Not connected
- Other error codes on failure

**Example:**
```c
char scene[256] = {0};
obsws_error_t err = obsws_get_current_scene(conn, scene, sizeof(scene));
if (err == OBSWS_OK) {
    printf("Current scene: %s\n", scene);
}
```

### obsws_set_current_scene()

Switch to a different scene.

**Signature:**
```c
obsws_error_t obsws_set_current_scene(obsws_connection_t *conn, const char *scene_name, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `scene_name` - Name of scene to switch to
- `response` - Pointer to receive response (can be NULL)

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

**Example:**
```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_set_current_scene(conn, "Scene1", &response);
if (err == OBSWS_OK && response && response->success) {
    printf("Switched to Scene1\n");
}
obsws_response_free(response);
```

### obsws_get_scene_list()

Get list of all scenes.

**Signature:**
```c
obsws_error_t obsws_get_scene_list(obsws_connection_t *conn, char *scenes_json, size_t json_size);
```

**Parameters:**
- `conn` - Connection
- `scenes_json` - Buffer to receive JSON array
- `json_size` - Buffer size

**Returns:**
- `OBSWS_OK` - Scene list retrieved
- Other error codes on failure

**Example:**
```c
char scenes[4096] = {0};
obsws_error_t err = obsws_get_scene_list(conn, scenes, sizeof(scenes));
if (err == OBSWS_OK) {
    printf("Scenes: %s\n", scenes);
}
```

---

## Recording & Streaming

### obsws_start_recording()

Start recording.

**Signature:**
```c
obsws_error_t obsws_start_recording(obsws_connection_t *conn, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `response` - Pointer to receive response (can be NULL)

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

**Example:**
```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_start_recording(conn, &response);
if (err == OBSWS_OK && response && response->success) {
    printf("Recording started\n");
}
obsws_response_free(response);
```

### obsws_stop_recording()

Stop recording.

**Signature:**
```c
obsws_error_t obsws_stop_recording(obsws_connection_t *conn, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `response` - Pointer to receive response (can be NULL)

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

### obsws_get_recording_status()

Get current recording status.

**Signature:**
```c
obsws_error_t obsws_get_recording_status(obsws_connection_t *conn, bool *is_recording, bool *is_paused);
```

**Parameters:**
- `conn` - Connection
- `is_recording` - Pointer to receive recording status
- `is_paused` - Pointer to receive pause status

**Returns:**
- `OBSWS_OK` - Status retrieved
- Other error codes on failure

**Example:**
```c
bool recording = false, paused = false;
obsws_error_t err = obsws_get_recording_status(conn, &recording, &paused);
if (err == OBSWS_OK) {
    printf("Recording: %s, Paused: %s\n", recording ? "yes" : "no", paused ? "yes" : "no");
}
```

### obsws_start_streaming()

Start streaming.

**Signature:**
```c
obsws_error_t obsws_start_streaming(obsws_connection_t *conn, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `response` - Pointer to receive response (can be NULL)

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

### obsws_stop_streaming()

Stop streaming.

**Signature:**
```c
obsws_error_t obsws_stop_streaming(obsws_connection_t *conn, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `response` - Pointer to receive response (can be NULL)

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

### obsws_get_streaming_status()

Get current streaming status.

**Signature:**
```c
obsws_error_t obsws_get_streaming_status(obsws_connection_t *conn, bool *is_streaming, bool *is_reconnecting);
```

**Parameters:**
- `conn` - Connection
- `is_streaming` - Pointer to receive streaming status
- `is_reconnecting` - Pointer to receive reconnecting status

**Returns:**
- `OBSWS_OK` - Status retrieved
- Other error codes on failure

---

## Source Control

### obsws_set_source_visibility()

Show or hide a scene item (source).

**Signature:**
```c
obsws_error_t obsws_set_source_visibility(obsws_connection_t *conn, const char *scene_name, 
                                          const char *source_name, bool visible, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `scene_name` - Scene containing the item
- `source_name` - Name of source/item
- `visible` - true to show, false to hide
- `response` - Pointer to receive response

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

**Example:**
```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_set_source_visibility(conn, "Scene1", "Camera", true, &response);
if (err == OBSWS_OK && response && response->success) {
    printf("Camera shown\n");
}
obsws_response_free(response);
```

### obsws_set_source_filter_enabled()

Enable or disable a filter on a source.

**Signature:**
```c
obsws_error_t obsws_set_source_filter_enabled(obsws_connection_t *conn, const char *source_name, 
                                              const char *filter_name, bool enabled, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `source_name` - Source name
- `filter_name` - Filter name
- `enabled` - true to enable, false to disable
- `response` - Pointer to receive response

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

### obsws_get_input_mute()

Check if an input is muted.

**Signature:**
```c
obsws_error_t obsws_get_input_mute(obsws_connection_t *conn, const char *input_name, bool *is_muted);
```

**Parameters:**
- `conn` - Connection
- `input_name` - Input name
- `is_muted` - Pointer to receive mute status

**Returns:**
- `OBSWS_OK` - Status retrieved
- Other error codes on failure

**Example:**
```c
bool muted = false;
obsws_error_t err = obsws_get_input_mute(conn, "Microphone", &muted);
if (err == OBSWS_OK) {
    printf("Muted: %s\n", muted ? "yes" : "no");
}
```

### obsws_set_input_mute()

Mute or unmute an input.

**Signature:**
```c
obsws_error_t obsws_set_input_mute(obsws_connection_t *conn, const char *input_name, 
                                   bool mute, obsws_response_t **response);
```

**Parameters:**
- `conn` - Connection
- `input_name` - Input name
- `mute` - true to mute, false to unmute
- `response` - Pointer to receive response

**Returns:**
- `OBSWS_OK` if request sent
- Check `response->success` for operation success

---

## Generic Requests

### obsws_send_request()

Send a generic request to OBS and wait for response.

**Signature:**
```c
obsws_error_t obsws_send_request(obsws_connection_t *conn, const char *request_type,
                                 const char *request_data, obsws_response_t **response, uint32_t timeout_ms);
```

**Parameters:**
- `conn` - Connection
- `request_type` - OBS request type (e.g., "GetVersion", "SetCurrentProgramScene")
- `request_data` - Optional JSON string with request parameters
- `response` - Pointer to receive response (can be NULL)
- `timeout_ms` - Timeout in milliseconds (0 = use configured default)

**Returns:**
- `OBSWS_OK` if response received
- `OBSWS_ERROR_TIMEOUT` if no response within timeout
- Other error codes on failure

**Description:**
This is the core function for all OBS operations. The library provides convenience functions for common operations, but this function can be used for any OBS WebSocket request.

**Example:**
```c
obsws_response_t *response = NULL;
const char *data = "{\"sceneName\": \"Scene1\"}";
obsws_error_t err = obsws_send_request(conn, "SetCurrentProgramScene", data, &response, 0);

if (err == OBSWS_OK && response) {
    printf("Success: %s\n", response->success ? "yes" : "no");
    if (response->response_data) {
        printf("Data: %s\n", response->response_data);
    }
    obsws_response_free(response);
}
```

### obsws_response_free()

Free response memory.

**Signature:**
```c
void obsws_response_free(obsws_response_t *response);
```

**Parameters:**
- `response` - Response to free (can be NULL)

**Description:**
Must be called for every non-NULL response received from `obsws_send_request()`. Safe to call with NULL.

---

## Status & Monitoring

### obsws_get_stats()

Get connection statistics.

**Signature:**
```c
obsws_error_t obsws_get_stats(obsws_connection_t *conn, obsws_stats_t *stats);
```

**Parameters:**
- `conn` - Connection
- `stats` - Pointer to receive statistics

**Returns:**
- `OBSWS_OK` - Statistics retrieved
- Other error codes on failure

**Example:**
```c
obsws_stats_t stats = {0};
obsws_error_t err = obsws_get_stats(conn, &stats);
if (err == OBSWS_OK) {
    printf("Messages sent: %lu\n", stats.messages_sent);
    printf("Messages received: %lu\n", stats.messages_received);
    printf("Bytes sent: %lu\n", stats.bytes_sent);
    printf("Bytes received: %lu\n", stats.bytes_received);
    printf("Errors: %lu\n", stats.error_count);
}
```

### obsws_process_events()

Process pending events for connection.

**Signature:**
```c
void obsws_process_events(obsws_connection_t *conn, int timeout_ms);
```

**Parameters:**
- `conn` - Connection
- `timeout_ms` - Time to wait for events (0 = don't wait)

**Description:**
Should be called periodically to allow background event thread to process messages. Alternatively, use event callbacks.

**Example:**
```c
// Simple polling loop
while (running) {
    obsws_process_events(conn, 100);
    // Do other work
}
```

### obsws_version()

Get library version string.

**Signature:**
```c
const char *obsws_version(void);
```

**Returns:**
- Version string (e.g., "1.0.0")

---

## Types & Structures

### obsws_error_t

Error code enumeration.

```c
typedef enum {
    OBSWS_OK = 0,
    OBSWS_ERROR_INVALID_PARAM = -1,
    OBSWS_ERROR_CONNECTION_FAILED = -2,
    OBSWS_ERROR_AUTH_FAILED = -3,
    OBSWS_ERROR_SEND_FAILED = -5,
    OBSWS_ERROR_RECV_FAILED = -6,
    OBSWS_ERROR_NOT_CONNECTED = -7,
    OBSWS_ERROR_OUT_OF_MEMORY = -8,
    OBSWS_ERROR_TIMEOUT = -9,
    OBSWS_ERROR_INVALID_RESPONSE = -10
} obsws_error_t;
```

### obsws_state_t

Connection state enumeration.

```c
typedef enum {
    OBSWS_STATE_DISCONNECTED,
    OBSWS_STATE_CONNECTING,
    OBSWS_STATE_AUTHENTICATING,
    OBSWS_STATE_CONNECTED
} obsws_state_t;
```

### obsws_log_level_t

Logging level enumeration.

```c
typedef enum {
    OBSWS_LOG_ERROR,
    OBSWS_LOG_WARNING,
    OBSWS_LOG_INFO,
    OBSWS_LOG_DEBUG
} obsws_log_level_t;
```

### obsws_debug_level_t

Debug level enumeration.

```c
typedef enum {
    OBSWS_DEBUG_NONE,
    OBSWS_DEBUG_LOW,
    OBSWS_DEBUG_MEDIUM,
    OBSWS_DEBUG_HIGH
} obsws_debug_level_t;
```

### obsws_config_t

Connection configuration structure.

```c
typedef struct {
    /* Connection parameters */
    const char *host;                    // OBS host (default: "localhost")
    int port;                            // WebSocket port (default: 4455)
    const char *password;                // Authentication password
    bool use_ssl;                        // Enable TLS/SSL
    
    /* Timeout settings (milliseconds) */
    int recv_timeout_ms;                 // Receive timeout (default: 30000)
    int send_timeout_ms;                 // Send timeout (default: 30000)
    
    /* Auto-reconnect settings */
    bool auto_reconnect;                 // Enable auto-reconnect
    int reconnect_delay_ms;              // Initial delay (default: 2000)
    int max_reconnect_delay_ms;          // Maximum delay (default: 10000)
    int max_reconnect_attempts;          // Max attempts (default: 10)
    
    /* Keep-alive */
    int ping_interval_ms;                // Ping interval (default: 20000)
    
    /* Callbacks */
    obsws_log_callback_t log_callback;
    obsws_event_callback_t event_callback;
    obsws_state_callback_t state_callback;
    void *user_data;                     // User-defined data for callbacks
    
    /* Logging */
    const char *log_directory;           // Directory for log files
} obsws_config_t;
```

### obsws_response_t

Response data structure.

```c
typedef struct {
    bool success;                        // Operation success flag
    char *response_data;                 // JSON response data
    const char *error_message;           // Error message if failed
} obsws_response_t;
```

### obsws_stats_t

Connection statistics structure.

```c
typedef struct {
    uint64_t messages_sent;              // Number of messages sent
    uint64_t messages_received;          // Number of messages received
    uint64_t bytes_sent;                 // Total bytes sent
    uint64_t bytes_received;             // Total bytes received
    uint64_t reconnect_count;            // Number of reconnections
    uint64_t error_count;                // Number of errors
} obsws_stats_t;
```

---

## Error Handling

### obsws_error_string()

Convert error code to human-readable string.

**Signature:**
```c
const char *obsws_error_string(obsws_error_t error);
```

**Parameters:**
- `error` - Error code

**Returns:**
- Descriptive error message

**Example:**
```c
obsws_error_t err = obsws_send_request(conn, "GetVersion", NULL, &response, 0);
if (err != OBSWS_OK) {
    fprintf(stderr, "Error: %s\n", obsws_error_string(err));
}
```

---

## Callbacks

### obsws_log_callback_t

Logging callback function type.

**Signature:**
```c
typedef void (*obsws_log_callback_t)(obsws_log_level_t level, const char *message, void *user_data);
```

**Parameters:**
- `level` - Log level
- `message` - Log message
- `user_data` - User-defined data

**Example:**
```c
void my_log_callback(obsws_log_level_t level, const char *message, void *user_data) {
    const char *level_str;
    switch (level) {
        case OBSWS_LOG_ERROR:   level_str = "ERROR"; break;
        case OBSWS_LOG_WARNING: level_str = "WARN";  break;
        case OBSWS_LOG_INFO:    level_str = "INFO";  break;
        case OBSWS_LOG_DEBUG:   level_str = "DEBUG"; break;
        default:                level_str = "?";     break;
    }
    printf("[%s] %s\n", level_str, message);
}

config.log_callback = my_log_callback;
config.user_data = (void *)0;
```

### obsws_event_callback_t

Event notification callback function type.

**Signature:**
```c
typedef void (*obsws_event_callback_t)(obsws_connection_t *conn, const char *event_type,
                                       const char *event_data, void *user_data);
```

**Parameters:**
- `conn` - Connection that received event
- `event_type` - Event type name (e.g., "SceneChanged")
- `event_data` - JSON event data
- `user_data` - User-defined data

**Example:**
```c
void my_event_callback(obsws_connection_t *conn, const char *event_type,
                       const char *event_data, void *user_data) {
    printf("Event: %s\nData: %s\n", event_type, event_data);
}

config.event_callback = my_event_callback;
```

### obsws_state_callback_t

Connection state change callback function type.

**Signature:**
```c
typedef void (*obsws_state_callback_t)(obsws_connection_t *conn, obsws_state_t old_state,
                                       obsws_state_t new_state, void *user_data);
```

**Parameters:**
- `conn` - Connection that changed state
- `old_state` - Previous state
- `new_state` - New state
- `user_data` - User-defined data

**Example:**
```c
void my_state_callback(obsws_connection_t *conn, obsws_state_t old_state,
                       obsws_state_t new_state, void *user_data) {
    printf("State: %s -> %s\n", obsws_state_string(old_state), obsws_state_string(new_state));
}

config.state_callback = my_state_callback;
```

---

## Constants

### Default Configuration Values

```c
#define OBSWS_DEFAULT_HOST              "localhost"
#define OBSWS_DEFAULT_PORT              4455
#define OBSWS_DEFAULT_PASSWORD          ""
#define OBSWS_DEFAULT_RECV_TIMEOUT_MS   30000
#define OBSWS_DEFAULT_SEND_TIMEOUT_MS   30000
#define OBSWS_DEFAULT_AUTO_RECONNECT    true
#define OBSWS_DEFAULT_RECONNECT_DELAY   2000
#define OBSWS_DEFAULT_MAX_RECONNECT_DELAY  10000
#define OBSWS_DEFAULT_MAX_RECONNECT_ATTEMPTS 10
#define OBSWS_DEFAULT_PING_INTERVAL     20000
```

### Library Version

```c
#define OBSWS_VERSION "1.0.0"
#define OBSWS_PROTOCOL_VERSION 1  // OBS WebSocket v5 RPC version
```

---

## Common Patterns

### Safe Request Handling

```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_send_request(conn, "GetVersion", NULL, &response, 0);

if (err != OBSWS_OK) {
    fprintf(stderr, "Request failed: %s\n", obsws_error_string(err));
    // Handle error
} else if (!response) {
    fprintf(stderr, "No response\n");
    // Handle no response
} else if (!response->success) {
    fprintf(stderr, "OBS error: %s\n", response->error_message);
    // Handle OBS error
} else {
    printf("Success: %s\n", response->response_data);
    // Handle success
}

obsws_response_free(response);
```

### Connection Polling Loop

```c
// Wait for connection with timeout
int timeout = 10000;  // 10 seconds
int elapsed = 0;
while (!obsws_is_connected(conn) && elapsed < timeout) {
    obsws_process_events(conn, 100);
    elapsed += 100;
}

if (!obsws_is_connected(conn)) {
    fprintf(stderr, "Connection timeout\n");
}
```

### Multi-Connection Management

```c
obsws_connection_t *conns[3];

// Connect to multiple OBS instances
for (int i = 0; i < 3; i++) {
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = hosts[i];
    config.port = ports[i];
    config.password = passwords[i];
    
    conns[i] = obsws_connect(&config);
}

// Process all connections
while (running) {
    for (int i = 0; i < 3; i++) {
        if (conns[i] && obsws_is_connected(conns[i])) {
            obsws_process_events(conns[i], 10);
        }
    }
}

// Cleanup
for (int i = 0; i < 3; i++) {
    if (conns[i]) obsws_disconnect(conns[i]);
}
```

---

**Author:** Aidan A. Bradley  
**License:** MIT  
**Last Updated:** November 2, 2024
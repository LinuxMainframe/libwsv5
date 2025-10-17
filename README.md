# libwsv5 - OBS WebSocket v5 Protocol Library

**Version: 1.0.0**

A C library for communicating with OBS Studio using the WebSocket v5 protocol. Handles multiple OBS connections for streaming setups, live production work, and automated recording/streaming systems.

## Table of Contents

- [Overview & Architecture](#overview--architecture)
- [How It Works](#how-it-works)
- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Quick Start](#quick-start)
- [API Overview](#api-overview)
- [Testing](#testing)
- [Configuration](#configuration)
- [Examples](#examples)
- [Multi-Stream Setup](#multi-stream-setup)
- [Error Handling](#error-handling)
- [Thread Safety](#thread-safety)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Overview & Architecture

### High-Level System Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    Your Application (C Program)                  │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              libwsv5 Library API                          │   │
│  │  - obsws_connect()      - obsws_set_current_scene()     │   │
│  │  - obsws_disconnect()   - obsws_get_scene_list()        │   │
│  │  - obsws_send_request() - obsws_start_recording()       │   │
│  │  - obsws_get_stats()    - obsws_process_events()        │   │
│  └──────────────────────────────────────────────────────────┘   │
└────────────────────┬─────────────────────────────────────────────┘
                     │
         ┌───────────┴──────────────┐
         │  WebSocket Connection    │
         │  (libwebsockets)         │
         │  TLS/SSL Optional        │
         └───────────┬──────────────┘
                     │
    ╔════════════════╩═════════════════╗
    ║   OBS Studio (localhost:4455)    ║
    ║   WebSocket Server               ║
    ║   - Scene Management             ║
    ║   - Recording/Streaming Control  ║
    ║   - Event Broadcasting           ║
    ║   - Statistics & Monitoring      ║
    ╚═════════════════════════════════╝
```

## How It Works

### 1. Connection Lifecycle

```
┌─────────────┐
│  INIT       │  Library initialization
│  obsws_init │
└──────┬──────┘
       │
       ▼
┌──────────────────────────────────────┐
│  CONFIG                              │
│  obsws_config_init(&config)          │
│  Set: host, port, password, etc.     │
└──────┬───────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────┐
│  CONNECT (TCP Handshake)             │
│  obsws_connection_t *conn =          │
│    obsws_connect(&config)            │
└──────┬───────────────────────────────┘
       │
       ▼ (WebSocket connection opens)
┌──────────────────────────────────────┐
│  AUTHENTICATION                      │
│  OBS sends HELLO with auth challenge │
│  Library sends IDENTIFY with response│
└──────┬───────────────────────────────┘
       │
       ▼ (Password verified)
┌──────────────────────────────────────┐
│  OPERATIONAL (READY)                 │
│  Connection state: OBSWS_CONNECTED   │
│  Can now send requests and get events│
└──────┬───────────────────────────────┘
       │
       ├─► Send Request ────► Process Request ──► Get Response
       ├─► Receive Events ◄── OBS Event Stream
       ├─► Health Check (ping/pong)
       │
       ▼
┌──────────────────────────────────────┐
│  CLEANUP                             │
│  obsws_disconnect(conn)              │
│  obsws_cleanup()                     │
└──────────────────────────────────────┘
```

### 2. Request-Response Communication Flow

```
┌─────────────────────────┐
│  Your Application       │
│  (Main Thread)          │
└───────────┬─────────────┘
            │
            │ obsws_send_request(conn, "SetCurrentScene", 
            │                     "{\"sceneName\":\"Game\"}")
            │
            ▼
    ┌───────────────────────────────────┐
    │  libwsv5 Library                  │
    │                                   │
    │  Request Handler Thread:          │
    │  ┌─────────────────────────────┐ │
    │  │ 1. Generate unique ID       │ │
    │  │    (UUID v4)                │ │
    │  │ 2. Build JSON payload       │ │
    │  │ 3. Send via WebSocket       │ │
    │  │ 4. Store in pending queue   │ │
    │  │ 5. Wait for response        │ │
    │  └─────────────────────────────┘ │
    │                                   │
    │  Response Handler Thread:         │
    │  ┌─────────────────────────────┐ │
    │  │ 1. Receive message          │ │
    │  │ 2. Parse JSON               │ │
    │  │ 3. Find matching request    │ │
    │  │    by ID                    │
    │  │ 4. Store response data      │ │
    │  │ 5. Signal waiting thread    │ │
    │  └─────────────────────────────┘ │
    └───────────────────────────────────┘
            │
            │ Response with status & data
            │
            ▼
    ┌──────────────────────────┐
    │ Return to Application    │
    │ - Success/Error Code     │
    │ - Response Data (JSON)   │
    │ - Statistics Updated     │
    └──────────────────────────┘
```

### 3. Event System Architecture

```
    ┌──────────────────────────────────────┐
    │  OBS Studio (Event Source)           │
    │  Broadcasts events to all clients    │
    │  - SceneChanged                      │
    │  - RecordingStateChanged             │
    │  - StreamingStateChanged             │
    │  - SceneItemEnabledStateChanged      │
    │  - And 50+ other event types         │
    └──────────────────┬───────────────────┘
                       │
        ┌──────────────┴────────────────┐
        │  WebSocket Connection         │
        │  (Binary/Text frames)         │
        └──────────────┬────────────────┘
                       │
    ┌──────────────────▼─────────────────────────┐
    │  libwsv5 Library                           │
    │  Event Reception Thread                    │
    │  ┌─────────────────────────────────────┐  │
    │  │ 1. Listen on WebSocket              │  │
    │  │ 2. Receive EVENT frames (opcode 5) │  │
    │  │ 3. Parse JSON: event_type, data    │  │
    │  │ 4. Call user_event_callback()      │  │
    │  │ 5. Continue listening              │  │
    │  └─────────────────────────────────────┘  │
    └──────────────────┬────────────────────────┘
                       │
                       ▼
    ┌──────────────────────────────────────────┐
    │  Your Application                        │
    │  Event Callback Function                 │
    │                                          │
    │  event_callback(conn, "SceneChanged",   │
    │                 "{...event_data...}",   │
    │                 user_data)              │
    │                                          │
    │  - Parse event_data JSON                │
    │  - Update UI                            │
    │  - Trigger related operations           │
    └──────────────────────────────────────────┘
```

### 4. Internal Library Structure

```
┌──────────────────────────────────────────────────────────────┐
│  libwsv5 Internal Architecture                              │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Configuration Layer                                  │ │
│  │  - obsws_config_t: Connection parameters             │ │
│  │  - Timeouts, SSL, auto-reconnect settings            │ │
│  │  - Callback function pointers                        │ │
│  └────────────────────────────────────────────────────────┘ │
│                          ▲                                   │
│                          │                                   │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  Connection Management (obsws_connection_t)           │ │
│  │  - WebSocket handle (lws context)                     │ │
│  │  - Connection state machine                           │ │
│  │  - Thread synchronization (mutexes)                   │ │
│  │  - Pending requests queue (UUID -> response map)      │ │
│  │  - Statistics tracker                                 │ │
│  └────────────────────────────────────────────────────────┘ │
│       ▲              ▲              ▲              ▲         │
│       │              │              │              │         │
│  ┌────┴──┐  ┌───────┴──┐  ┌────────┴────┐  ┌──────┴───┐   │
│  │Request │  │ Response │  │ Event       │  │Auth      │   │
│  │Handler │  │ Handler  │  │ Handler     │  │Handler   │   │
│  │Thread  │  │ Thread   │  │ Thread      │  │Thread    │   │
│  └────────┘  └──────────┘  └─────────────┘  └──────────┘   │
│  - Sends     - Receives   - Listens for - SHA-256+Base64  │
│    requests    responses    OBS events    auth sequence   │
│  - Handles   - Matches    - Parses    - Handshake       │
│    timeouts    responses    JSON       protocol v5       │
│  - Retries   - Signals    - Calls     - Reconnection    │
│              awaiting     callbacks                        │
│              threads                                       │
│                                                            │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  WebSocket Layer (libwebsockets)                      │ │
│  │  - TCP connection management                          │ │
│  │  - WebSocket frame encoding/decoding                  │ │
│  │  - SSL/TLS support (optional)                         │ │
│  │  - ping/pong keep-alive                               │ │
│  └────────────────────────────────────────────────────────┘ │
│       ▲                                                      │
│       │  System Sockets (TCP/IP)                            │
│       │                                                      │
└───────┼──────────────────────────────────────────────────────┘
        │
        ▼
   [Network - OBS Server]
```

### 5. Thread Model

```
┌──────────────────────────────────────────────────────────────┐
│  Thread Architecture                                         │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Main Application Thread(s)                                 │
│  ├─► Call obsws_send_request() - blocks until response     │
│  ├─► Call obsws_set_current_scene() - async to OBS        │
│  ├─► Call obsws_get_scene_list() - queries OBS            │
│  ├─► Check obsws_is_connected() - state check             │
│  └─► Register callbacks (log, event, state callbacks)     │
│                                                              │
│          ▼ (Library spawns background threads)              │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Background Thread Pool (per connection)            │  │
│  │                                                      │  │
│  │  1. WebSocket I/O Thread                           │  │
│  │     - Handles TCP/WebSocket communication          │  │
│  │     - Event-driven (select/poll/epoll)             │  │
│  │     - Sends/receives frames                        │  │
│  │     - Maintains connection health (ping/pong)      │  │
│  │                                                      │  │
│  │  2. Message Processing Thread                      │  │
│  │     - Parses incoming WebSocket frames             │  │
│  │     - Determines message type (Request/Event/etc) │  │
│  │     - Routes to appropriate handler                │  │
│  │     - Calls user callbacks (from this thread)      │  │
│  │                                                      │  │
│  │  3. Request/Response Matching Engine               │  │
│  │     - Sends outgoing requests                      │  │
│  │     - Tracks pending requests by UUID              │  │
│  │     - Matches responses to requests                │  │
│  │     - Handles timeouts                             │  │
│  │                                                      │  │
│  │  All threads protected by:                         │  │
│  │  - Mutexes for state access                        │  │
│  │  - Condition variables for signaling               │  │
│  │  - Thread-safe queues for message passing          │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
│  Thread Safety Guarantees:                                  │
│  - All public API functions are thread-safe               │
│  - Multiple threads can call API simultaneously           │
│  - Callbacks are serialized (called from library thread) │
│  - No data corruption, even with concurrent access       │
└──────────────────────────────────────────────────────────────┘
```

### 6. Authentication Flow (OBS WebSocket v5)

```
Client (libwsv5)                    OBS Server
      │                                 │
      │◄────────────── HELLO ──────────┤
      │ {opcode: 0,                    │
      │  d: {                          │
      │    negotiatedRpcVersion: 1,    │
      │    authentication: {           │
      │      challenge: "xxxxx",       │
      │      salt: "yyyyy"             │
      │    }                           │
      │  }                             │
      │                                │
      │ (Library receives HELLO)       │
      │ (Computes: SHA256(password +  │
      │  salt) = hash1)               │
      │ (Computes: SHA256(hash1 +     │
      │  challenge) = hash2)          │
      │ (Base64-encodes hash2)        │
      │                                │
      ├─────── IDENTIFY ─────────────►│
      │ {opcode: 1,                   │
      │  d: {                         │
      │    rpcVersion: 1,             │
      │    authentication: "base64(hash2)",
      │    ignoreNonFatalRequestChecks: false
      │  }                            │
      │                               │
      │                        (Verifies auth)
      │                               │
      │◄────── IDENTIFIED ────────────┤
      │ {opcode: 2}                   │
      │                               │
      │ ✓ AUTHENTICATED & READY       │
      │                               │
      ├─ Subscribe to Events ────────►│
      │ {opcode: 6,                   │
      │  d: {                         │
      │    requestType: "SetEventSubscriptions",
      │    requestData: { eventSubscriptions: 2047 }
      │  }                            │
      │                               │
      │ ✓ Connection Fully Operational│
      └───────────────────────────────┘
```

### 7. Message Protocol Overview

```
All Communication is JSON-RPC 2.0 over WebSocket Frames

REQUEST (Client → OBS):
{
  "op": 6,                      // Opcode: REQUEST
  "d": {
    "requestType": "SetCurrentScene",
    "requestId": "550e8400-e29b-41d4-a716-446655440000",
    "requestData": {
      "sceneName": "Gameplay"
    }
  }
}

RESPONSE (OBS → Client):
{
  "op": 7,                      // Opcode: REQUEST_RESPONSE  
  "d": {
    "requestType": "SetCurrentScene",
    "requestId": "550e8400-e29b-41d4-a716-446655440000",
    "requestStatus": {
      "result": true,           // Success
      "code": 100               // HTTP-like code
    },
    "responseData": {}           // Optional, depends on request
  }
}

EVENT (OBS → Client, unsolicited):
{
  "op": 5,                      // Opcode: EVENT
  "d": {
    "subscriptionStatus": 0,    // For event subscriptions
    "eventType": "SceneChanged",
    "eventIntent": 0,
    "eventData": {
      "sceneName": "Gameplay",
      "sceneUuid": "550e8400-e29b-41d4-a716-446655440000"
    }
  }
}
```

## Features

- Full OBS WebSocket v5 Protocol Support - Implements OBS WebSocket API v1
- Connection Management - Automatic reconnection, health monitoring (ping/pong), keep-alive
- Thread-Safe Operations - Safe for multi-threaded applications with mutex protection
- Multiple Concurrent Connections - Manage multiple OBS instances at the same time
- Scene Management - Switch scenes, get scene lists, manage scene collections
- Scene Item Control - Manipulate scene items (visibility, position, scale, rotation)
- Recording & Streaming Control - Start/stop recording and streaming with status tracking
- Event System - Subscribe to OBS events with user-defined callbacks
- Authentication - SHA256+Base64 password-based authentication
- Error Handling - Error codes with recovery mechanisms
- Statistics & Monitoring - Track connection health, message counts, latency, errors
- Debug Levels - Configurable debug output (0-3) for development and production
- Documentation - Doxygen-generated HTML/PDF with source browsing and call graphs

## Requirements

### Build Dependencies

- **CMake** >= 3.10 (optional, Makefile also provided)
- **GCC** or **Clang** (C11 support)
- **OpenSSL** (libssl-dev)
- **libwebsockets** (libwebsockets-dev)
- **cJSON** (libcjson-dev)
- **pthreads** (usually included with system)

### Installing Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libssl-dev libwebsockets-dev libcjson-dev
```

#### Fedora/RHEL
```bash
sudo dnf install gcc cmake openssl-devel libwebsockets-devel cjson-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake openssl libwebsockets cjson
```

#### macOS (Homebrew)
```bash
brew install cmake openssl libwebsockets cjson
```

## Building

### Using Makefile (Recommended)

```bash
# Build library and test
make

# Clean build files
make clean

# Build and run test
make test

# Generate documentation
make doc

# Install system-wide
sudo make install
```

### Using CMake

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make

# Optional: Install system-wide
sudo make install
```

## Quick Start

### Basic Usage

```c
#include <libwsv5/library.h>

int main() {
    // Initialize library
    obsws_init();
    
    // Configure connection
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.port = 4455;
    config.password = "your_password";
    
    // Connect to OBS
    obsws_connection_t *conn = obsws_connect(&config);
    
    // Wait for connection
    while (!obsws_is_connected(conn)) {
        usleep(100000); // 100ms
    }
    
    // Switch scene
    obsws_set_current_scene(conn, "Game Scene", NULL);
    
    // Disconnect
    obsws_disconnect(conn);
    obsws_cleanup();
    
    return 0;
}
```

### Compile Your Program

```bash
gcc -o myprogram myprogram.c -lwsv5 -lwebsockets -lcjson -lssl -lcrypto -lpthread -lm
```

Or with CMake:

```cmake
find_library(LIBWSV5 wsv5)
target_link_libraries(myprogram ${LIBWSV5})
```

## API Overview

### Initialization

```c
obsws_error_t obsws_init(void);
void obsws_cleanup(void);
const char* obsws_version(void);
void obsws_set_log_level(obsws_log_level_t level);
void obsws_set_debug_level(obsws_debug_level_t level);
int obsws_get_debug_level(void);
```

### Connection Management

```c
void obsws_config_init(obsws_config_t *config);
obsws_connection_t* obsws_connect(const obsws_config_t *config);
void obsws_disconnect(obsws_connection_t *conn);
bool obsws_is_connected(const obsws_connection_t *conn);
obsws_state_t obsws_get_state(const obsws_connection_t *conn);
obsws_error_t obsws_get_stats(const obsws_connection_t *conn, obsws_stats_t *stats);
```

### Scene Management

```c
obsws_error_t obsws_set_current_scene(obsws_connection_t *conn, const char *scene_name, obsws_response_t **response);
obsws_error_t obsws_get_current_scene(obsws_connection_t *conn, char *scene_name, size_t buffer_size);
obsws_error_t obsws_get_scene_list(obsws_connection_t *conn, char ***scenes, size_t *count);
```

### Scene Item Control

Scene items can be controlled using the generic `obsws_send_request()` function:

```c
// Get scene items
char request[512];
snprintf(request, sizeof(request), "{\"sceneName\":\"%s\"}", scene_name);
obsws_send_request(conn, "GetSceneItemList", request, &response, 0);

// Hide/show scene item
snprintf(request, sizeof(request), 
         "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemEnabled\":%s}", 
         scene_name, item_id, enabled ? "true" : "false");
obsws_send_request(conn, "SetSceneItemEnabled", request, &response, 0);
```

### Recording & Streaming

```c
obsws_error_t obsws_start_recording(obsws_connection_t *conn, obsws_response_t **response);
obsws_error_t obsws_stop_recording(obsws_connection_t *conn, obsws_response_t **response);
obsws_error_t obsws_start_streaming(obsws_connection_t *conn, obsws_response_t **response);
obsws_error_t obsws_stop_streaming(obsws_connection_t *conn, obsws_response_t **response);
```

### Custom Requests

```c
obsws_error_t obsws_send_request(obsws_connection_t *conn, const char *request_type, 
                                 const char *request_data, obsws_response_t **response, uint32_t timeout_ms);
```

## Testing

### Comprehensive Test Suite

The library includes a comprehensive test suite that validates all functionality:

```bash
# Build the test
make

# Run with default settings (localhost, no password)
./tests/comprehensive_test

# Run with custom OBS instance
./tests/comprehensive_test --host 192.168.1.13 --password mypass

# Run with verbose debug output
./tests/comprehensive_test --host 192.168.1.13 --password mypass --debug 3

# Show all options
./tests/comprehensive_test --help
```

### What the Test Suite Covers

1. Library Initialization - Startup and configuration
2. Connection Establishment - WebSocket connection and authentication
3. Version Information - OBS version query
4. Scene List Retrieval - Get all available scenes
5. Current Scene Query - Get active scene
6. Scene Switching - Switch between multiple scenes (Test1-Test4)
7. Connection Statistics - Track messages, bytes, errors
8. Recording Control - Start/stop recording
9. Custom Requests - Generic request handling
10. Debug Level System - Test all debug levels (0-3)
11. Stress Testing - Rapid scene switching
12. Scene Item Manipulation - Hide/show, move, scale, rotate scene items
13. Event Reception - Receive and process OBS events
14. Clean Disconnection - Proper shutdown
15. Library Cleanup - Resource deallocation

## Configuration

### Connection Configuration

```c
obsws_config_t config;
obsws_config_init(&config);

// Required settings
config.host = "192.168.1.100";      // OBS host
config.port = 4455;                  // OBS WebSocket port
config.password = "your_password";   // OBS password (NULL if no auth)

// Optional settings
config.use_ssl = false;              // Use WSS instead of WS
config.connect_timeout_ms = 5000;    // Connection timeout
config.recv_timeout_ms = 5000;       // Receive timeout
config.send_timeout_ms = 5000;       // Send timeout
config.ping_interval_ms = 10000;     // Keep-alive ping interval
config.auto_reconnect = true;        // Enable auto-reconnection
config.reconnect_delay_ms = 1000;    // Initial reconnect delay
config.max_reconnect_delay_ms = 30000; // Max reconnect delay
config.max_reconnect_attempts = 0;   // 0 = infinite

// Callbacks
config.log_callback = my_log_callback;
config.event_callback = my_event_callback;
config.state_callback = my_state_callback;
config.user_data = my_user_data;
```

### Debug Levels

The library supports 4 debug levels for development and troubleshooting:

```c
// Set debug level (0-3)
obsws_set_debug_level(OBSWS_DEBUG_NONE);     // 0: No debug output (production)
obsws_set_debug_level(OBSWS_DEBUG_LOW);      // 1: Basic events, auth, scene changes
obsws_set_debug_level(OBSWS_DEBUG_MEDIUM);   // 2: + Opcodes, event types, request IDs
obsws_set_debug_level(OBSWS_DEBUG_HIGH);     // 3: + Full messages, passwords, JSON

// Get current debug level
int level = obsws_get_debug_level();
```

**Debug Level Details:**
- **Level 0 (NONE)**: Production mode - no debug output
- **Level 1 (LOW)**: Connection events, authentication, scene changes
- **Level 2 (MEDIUM)**: + WebSocket opcodes, event types, request/response tracking
- **Level 3 (HIGH/VERBOSE)**: + Full message contents, passwords, complete JSON payloads

## Examples

### Example 1: Simple Scene Switcher

```c
#include <libwsv5/library.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    obsws_init();
    
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.port = 4455;
    config.password = "your_password";
    
    obsws_connection_t *conn = obsws_connect(&config);
    
    // Wait for connection
    for (int i = 0; i < 50 && !obsws_is_connected(conn); i++) {
        usleep(100000);
    }
    
    if (obsws_is_connected(conn)) {
        // Switch scene
        obsws_set_current_scene(conn, "Gameplay", NULL);
        printf("Scene switched to Gameplay\n");
    }
    
    obsws_disconnect(conn);
    obsws_cleanup();
    return 0;
}
```

### Example 2: Multi-Scene Control with Events

```c
#include <libwsv5/library.h>
#include <stdio.h>
#include <unistd.h>
#include <cjson/cJSON.h>

void event_callback(obsws_connection_t *conn, const char *event_type, 
                   const char *event_data, void *user_data) {
    printf("Event: %s\n", event_type);
    
    if (strcmp(event_type, "SceneChanged") == 0) {
        cJSON *event = cJSON_Parse(event_data);
        if (event) {
            cJSON *data = cJSON_GetObjectItem(event, "eventData");
            if (data) {
                cJSON *scene = cJSON_GetObjectItem(data, "sceneName");
                if (scene) {
                    printf("  Scene switched to: %s\n", scene->valuestring);
                }
            }
            cJSON_Delete(event);
        }
    }
}

int main() {
    obsws_init();
    
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.port = 4455;
    config.password = "your_password";
    config.event_callback = event_callback;
    
    obsws_connection_t *conn = obsws_connect(&config);
    
    // Wait for connection
    for (int i = 0; i < 50 && !obsws_is_connected(conn); i++) {
        usleep(100000);
    }
    
    if (obsws_is_connected(conn)) {
        // Switch scenes
        obsws_set_current_scene(conn, "Scene1", NULL);
        sleep(2);
        obsws_set_current_scene(conn, "Scene2", NULL);
        
        // Keep running to receive events
        sleep(5);
    }
    
    obsws_disconnect(conn);
    obsws_cleanup();
    return 0;
}
```

### Example 3: Recording Control

```c
#include <libwsv5/library.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    obsws_init();
    
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.port = 4455;
    config.password = "your_password";
    
    obsws_connection_t *conn = obsws_connect(&config);
    
    // Wait for connection
    for (int i = 0; i < 50 && !obsws_is_connected(conn); i++) {
        usleep(100000);
    }
    
    if (obsws_is_connected(conn)) {
        printf("Starting recording...\n");
        obsws_start_recording(conn, NULL);
        
        sleep(10); // Record for 10 seconds
        
        printf("Stopping recording...\n");
        obsws_stop_recording(conn, NULL);
    }
    
    obsws_disconnect(conn);
    obsws_cleanup();
    return 0;
}
```

## Multi-Stream Setup

Example: Managing multiple OBS instances simultaneously

```c
#include <libwsv5/library.h>

int main() {
    obsws_init();
    
    // Create configs for multiple OBS instances
    obsws_config_t configs[3];
    obsws_connection_t *connections[3];
    
    // OBS Instance 1 - Main Broadcast
    obsws_config_init(&configs[0]);
    configs[0].host = "192.168.1.10";
    configs[0].port = 4455;
    configs[0].password = "pass1";
    
    // OBS Instance 2 - Recording PC
    obsws_config_init(&configs[1]);
    configs[1].host = "192.168.1.20";
    configs[1].port = 4455;
    configs[1].password = "pass2";
    
    // OBS Instance 3 - Archive PC
    obsws_config_init(&configs[2]);
    configs[2].host = "192.168.1.30";
    configs[2].port = 4455;
    configs[2].password = "pass3";
    
    // Connect all
    for (int i = 0; i < 3; i++) {
        connections[i] = obsws_connect(&configs[i]);
    }
    
    // Wait for all connections
    for (int i = 0; i < 3; i++) {
        int attempts = 0;
        while (!obsws_is_connected(connections[i]) && attempts++ < 50) {
            usleep(100000);
        }
    }
    
    // Synchronize scene changes across all OBS instances
    obsws_set_current_scene(connections[0], "Gameplay", NULL);
    obsws_set_current_scene(connections[1], "Gameplay", NULL);
    obsws_set_current_scene(connections[2], "Gameplay", NULL);
    
    // ... do work ...
    
    // Disconnect all
    for (int i = 0; i < 3; i++) {
        obsws_disconnect(connections[i]);
    }
    
    obsws_cleanup();
    return 0;
}
```

## Error Handling

The library returns error codes for success or failure. You should always check the return values:

```c
obsws_error_t err = obsws_set_current_scene(conn, "MyScene", NULL);

switch (err) {
    case OBSWS_OK:
        printf("Success!\n");
        break;
    case OBSWS_ERROR_NOT_CONNECTED:
        printf("Not connected to OBS\n");
        break;
    case OBSWS_ERROR_TIMEOUT:
        printf("Request timed out\n");
        break;
    case OBSWS_ERROR_INVALID_PARAM:
        printf("Invalid parameters\n");
        break;
    case OBSWS_ERROR_AUTH_FAILED:
        printf("Authentication failed (wrong password?)\n");
        break;
    default:
        printf("Error: %s\n", obsws_error_string(err));
        break;
}
```

## Thread Safety

All the library functions work safely with multiple threads. You can call them from different threads at the same time:

```c
// Thread 1: Monitor connection
void* monitor_thread(void *arg) {
    obsws_connection_t *conn = (obsws_connection_t *)arg;
    while (obsws_is_connected(conn)) {
        sleep(1);
    }
    return NULL;
}

// Thread 2: Send requests
void* request_thread(void *arg) {
    obsws_connection_t *conn = (obsws_connection_t *)arg;
    for (int i = 0; i < 10; i++) {
        obsws_set_current_scene(conn, "Scene1", NULL);
        sleep(1);
    }
    return NULL;
}

int main() {
    obsws_init();
    
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    
    obsws_connection_t *conn = obsws_connect(&config);
    
    pthread_t t1, t2;
    pthread_create(&t1, NULL, monitor_thread, conn);
    pthread_create(&t2, NULL, request_thread, conn);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    obsws_disconnect(conn);
    obsws_cleanup();
    return 0;
}
```

## Troubleshooting

### Connection refused / Cannot connect to OBS

1. **Verify OBS is running** - `ps aux | grep obs`
2. **Check WebSocket is enabled** - Tools → WebSocket Server Settings
3. **Verify port** - Should be 4455 (or custom port configured)
4. **Verify host** - localhost/127.0.0.1 for same machine, IP address for remote
5. **Check firewall** - May need to allow port 4455

### Authentication failed

1. **Wrong password** - Copy from Tools → WebSocket Server Settings
2. **No password set** - Set `config.password = NULL` if no auth required
3. **Password changed** - Update your code with new password

### Timeouts / Slow responses

1. **Network issues** - Check network connectivity
2. **OBS overloaded** - Close other applications
3. **Scene too complex** - Reduce number of sources in scene
4. **Increase timeout** - Set `config.recv_timeout_ms = 10000` (10 seconds)

### Event callbacks not being called

1. **Check subscription** - Library subscribes by default to most events
2. **Verify event type** - Check OBS logs for actual events
3. **Check callback is set** - `config.event_callback = my_callback`

## Documentation

For more detailed API documentation, you can generate Doxygen docs:

```bash
# Generate documentation
make doc

# View HTML
firefox doc/html/index.html

# View PDF
evince doc/latex/refman.pdf
```

The documentation includes:
- Complete API reference for all functions
- Data structure definitions
- Call graphs showing function relationships
- Source code browsing
- Cross-references and search functionality

## License

This library is provided as-is. Check the LICENSE file for details.

---

**libwsv5 Version: 1.0.0** | Last Updated: 2025 | OBS WebSocket Protocol v5
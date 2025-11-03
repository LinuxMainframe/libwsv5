# libwsv5 - OBS WebSocket v5 Protocol Library

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-11-blue.svg)]()
[![Version](https://img.shields.io/badge/version-1.1.0-brightgreen.svg)]()
[![Build macOS](https://img.shields.io/badge/macOS_Build-Passing-green)](https://img.shields.io/badge/macOS_Build-Passing-green)
[![Build macOS](https://img.shields.io/badge/Debian_Build-Passing-green)](https://img.shields.io/badge/Debian_Build-Passing-green)
[![Build macOS](https://img.shields.io/badge/Windows_Build-N/A-red)](https://img.shields.io/badge/Windows_Build-N/A-red)

A high-performance C library for communicating with OBS Studio via the WebSocket v5 protocol. Designed for streaming professionals, developers, and automation systems that need reliable control over OBS instances.

## Features

- **Multi-Connection Support** - Manage multiple OBS instances simultaneously
- **Automatic Reconnection** - Configurable reconnection with exponential backoff
- **Thread-Safe Operations** - Safe for multi-threaded applications
- **OBS WebSocket v5 Protocol** - Full implementation of protocol version 1 (RPC v1)
- **Authentication** - SHA256-based authentication with salt and challenge
- **Complete API Coverage** - Scene switching, recording control, source manipulation
- **Event Callbacks** - Real-time notifications of OBS state changes
- **Error Handling** - Comprehensive error codes and detailed status reporting
- **No External Dependencies** - Uses standard C libraries (libwebsockets, OpenSSL, cJSON)

---
## Why libwsv5?

**The fastest, most reliable OBS WebSocket v5 client for C/C++ applications.**

- **Zero-copy design** - Direct memory mapping, no Python/JS overhead
- **Production-tested** - Powers [ROCStreamer/Matt's Outback Paintball] serving 150+ users
- **Full protocol support** - 50+ OBS commands, all v5 features
- **Thread-safe** - Manage multiple OBS instances concurrently
- **Auto-reconnect** - Survives network hiccups and OBS crashes
- **Multi-Platform Support** - Currently Supports macOS and Linux natively with Windows support planned.

### Use Cases
- **Broadcast automation** - Trigger scene changes from external events
- **Stream directors** - Multi-camera switching applications
- **Recording managers** - Automated recording start/stop systems
- **Monitoring dashboards** - Real-time OBS status displays
- **Hardware controllers** - Stream decks, MIDI controllers, GPIO triggers

## Quick to Get Running (~30 seconds)
```c
#include 

int main() {
    obsws_init();
    
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.password = "your_password";
    
    obsws_connection_t *obs = obsws_connect(&config);
    obsws_set_current_scene(obs, "Gaming Scene", NULL);
    
    obsws_disconnect(obs);
    obsws_cleanup();
    return 0;
}
```
---

## Quick Start

### Prerequisites

Install required dependencies:

```bash
# Ubuntu/Debian
sudo apt-get install libwebsockets-dev libcjson-dev libssl-dev

# macOS
brew install libwebsockets cjson openssl

# Fedora/RHEL
sudo dnf install libwebsockets-devel cjson-devel openssl-devel
```

### Building

```bash
# Clone and build
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5
mkdir build && cd build
cmake -DBUILD_TESTS=ON ..
make

# Run tests
./test -h localhost -p 4455 -w your_obs_password
```

### Basic Usage

```c
#include "libwsv5.h"
#include <stdio.h>

int main() {
    // Initialize library
    obsws_init();
    
    // Configure connection
    obsws_config_t config;
    obsws_config_init(&config);
    config.host = "localhost";
    config.port = 4455;
    config.password = "obs_password";
    
    // Connect to OBS
    obsws_connection_t *conn = obsws_connect(&config);
    if (!conn) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }
    
    // Wait for connection
    for (int i = 0; i < 100; i++) {
        if (obsws_is_connected(conn)) break;
        obsws_process_events(conn, 100);
    }
    
    if (!obsws_is_connected(conn)) {
        fprintf(stderr, "Connection timeout\n");
        obsws_disconnect(conn);
        obsws_cleanup();
        return 1;
    }
    
    // Perform operations
    obsws_response_t *response = NULL;
    obsws_error_t err = obsws_send_request(conn, "GetVersion", NULL, &response, 0);
    
    if (err == OBSWS_OK && response && response->success) {
        printf("Successfully queried OBS version\n");
    }
    if (response) obsws_response_free(response);
    
    // Cleanup
    obsws_disconnect(conn);
    obsws_cleanup();
    
    return 0;
}
```

## Architecture

### System Overview

```
Your Application
    ↓
libwsv5 API
    ↓
WebSocket Connection (libwebsockets)
    ↓
OBS Studio (localhost:4455)
```

### Connection Lifecycle

1. **Initialize** - `obsws_init()` to set up the library
2. **Configure** - Create config with `obsws_config_init()` and set parameters
3. **Connect** - Use `obsws_connect()` to establish WebSocket connection
4. **Authenticate** - Library handles authentication automatically
5. **Operate** - Send requests and receive responses via `obsws_send_request()`
6. **Monitor** - Use callbacks to receive real-time events and state changes
7. **Cleanup** - Call `obsws_disconnect()` and `obsws_cleanup()`

## Key Concepts

### Requests and Responses

The library uses a request-response pattern where each request gets a corresponding response:

```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_send_request(conn, "GetCurrentProgramScene", 
                                       NULL, &response, 0);

if (err == OBSWS_OK && response && response->success) {
    // Operation succeeded - response contains data
} else if (err == OBSWS_ERROR_TIMEOUT) {
    // Request timed out - retry
} else if (err == OBSWS_ERROR_NOT_CONNECTED) {
    // Connection lost
}

obsws_response_free(response);
```

### Callbacks

Register callbacks for real-time notifications:

```c
void event_callback(obsws_connection_t *conn, const char *event_type,
                    const char *event_data, void *user_data) {
    printf("Event: %s\n", event_type);
}

config.event_callback = event_callback;
config.user_data = (void *)0;  // Custom data pointer
```

### Error Handling

All functions return `obsws_error_t`. Use `obsws_error_string()` for human-readable messages:

```c
obsws_error_t err = obsws_send_request(conn, "InvalidRequest", NULL, &response, 0);
if (err != OBSWS_OK) {
    printf("Error: %s\n", obsws_error_string(err));
}
```

## Common Operations

### Start Recording

```c
obsws_response_t *response = NULL;
obsws_error_t err = obsws_start_recording(conn, &response);
if (err == OBSWS_OK && response && response->success) {
    printf("Recording started\n");
}
obsws_response_free(response);
```

### Switch Scenes

```c
obsws_response_t *response = NULL;
const char *scene_json = "{\"sceneName\": \"Scene1\"}";
obsws_error_t err = obsws_send_request(conn, "SetCurrentProgramScene", 
                                       scene_json, &response, 0);
if (err == OBSWS_OK && response && response->success) {
    printf("Scene switched\n");
}
obsws_response_free(response);
```

### Get Current Scene

```c
char scene_name[256] = {0};
obsws_error_t err = obsws_get_current_scene(conn, scene_name, sizeof(scene_name));
if (err == OBSWS_OK) {
    printf("Current scene: %s\n", scene_name);
}
```

### Move Scene Item

```c
const char *transform_json = "{"
    "\"sceneName\": \"Scene1\","
    "\"sceneItemId\": 1,"
    "\"sceneItemTransform\": {"
    "  \"x\": 100.0,"
    "  \"y\": 50.0,"
    "  \"scaleX\": 1.0,"
    "  \"scaleY\": 1.0,"
    "  \"rotation\": 0.0"
    "}"
"}";

obsws_response_t *response = NULL;
obsws_error_t err = obsws_send_request(conn, "SetSceneItemTransform", 
                                       transform_json, &response, 0);
obsws_response_free(response);
```

## Configuration Options

Key configuration parameters:

- `host` - OBS WebSocket server address (default: "localhost")
- `port` - WebSocket server port (default: 4455)
- `password` - Authentication password (default: "")
- `use_ssl` - Enable TLS/SSL (default: false)
- `recv_timeout_ms` - Receive timeout (default: 30000ms)
- `send_timeout_ms` - Send timeout (default: 30000ms)
- `auto_reconnect` - Enable auto-reconnect (default: true)
- `reconnect_delay_ms` - Initial reconnection delay (default: 2000ms)
- `max_reconnect_delay_ms` - Maximum reconnection delay (default: 10000ms)
- `max_reconnect_attempts` - Max reconnection attempts (default: 10)
- `ping_interval_ms` - Keep-alive ping interval (default: 20000ms)

## Testing

Run the comprehensive test suite:

```bash
cd build
./test -h localhost -p 4455 -w obs_password -d 1
```

Test options:
- `-h, --host HOST` - OBS WebSocket host
- `-p, --port PORT` - WebSocket port
- `-w, --password PASS` - Authentication password
- `-d, --debug LEVEL` - Debug level (0-3)
- `--skip-multi` - Skip multi-connection tests
- `--skip-batch` - Skip batch request tests
- `--skip-transforms` - Skip scene transformation tests
- `--help` - Show help message

## Documentation

- **[API Reference](API_REFERENCE.md)** - Complete function and type documentation
- **[Changelog](CHANGELOG.md)** - Version history and changes
- **Doxygen Docs** - Generate with `make doc` (requires doxygen)
  - HTML: `build/doc/html/index.html`
  - PDF: `build/doc/latex/refman.pdf`

## Requirements

- **C11 compiler** (gcc, clang, etc.)
- **CMake 3.10+**
- **OpenSSL** (development files)
- **libwebsockets 3.0+**
- **cJSON** (libcjson)
- **POSIX-compliant system** (Linux, macOS, BSD)

Optional:
- **Doxygen** - For documentation generation
- **Graphviz** - For diagrams in documentation

## Troubleshooting

### Connection Refused
- Verify OBS is running and WebSocket enabled
- Check host and port settings
- Ensure firewall allows connections

### Authentication Failed
- Verify password is correct
- Check OBS WebSocket password setting
- Ensure OBS server is running expected version

### Timeout Errors
- Increase `recv_timeout_ms` if network is slow
- Check system resources (CPU, memory)
- Verify network connectivity

### Memory Issues
- Always call `obsws_response_free()` for responses
- Check for connection leaks with `obsws_disconnect()`
- Monitor with `obsws_get_stats()`

## Performance Characteristics

- **Single Connection** - Minimal overhead, suitable for simple automation
- **Multiple Connections** - Thread-per-connection design for scalability
- **Latency** - Typically 10-50ms per request (depends on network and OBS)
- **Throughput** - Hundreds of requests per second
- **Memory** - ~1MB per idle connection, more during high throughput

## Building from Source

### Linux / Ubuntu / Debian

```bash
# Install dependencies
sudo apt-get install build-essential cmake libwebsockets-dev libcjson-dev libssl-dev

# Clone repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Create build directory
mkdir build && cd build

# Configure
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# Install
sudo make install

# Test
./test -h localhost -p 4455 -w obs_password
```

### macOS

```bash
# Install dependencies via Homebrew
brew install libwebsockets cjson openssl cmake

# Clone repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Create build directory
mkdir build && cd build

# Configure (CMake automatically finds Homebrew packages)
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(sysctl -n hw.ncpu)

# Install (optional, adjust prefix as needed)
sudo make install

# Test
./test -h localhost -p 4455 -w obs_password
```

### Fedora / RHEL

```bash
# Install dependencies
sudo dnf install gcc cmake libwebsockets-devel cjson-devel openssl-devel

# Clone repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Create build directory
mkdir build && cd build

# Configure
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# Install
sudo make install

# Test
./test -h localhost -p 4455 -w obs_password
```

## License

MIT License - See LICENSE file for details

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes and commit with clear messages
4. Submit a pull request

## Support

For issues and questions:
- Check the [API Reference](API_REFERENCE.md)
- Review the test suite in `tests/test.c`
- Generate documentation with Doxygen

## Author

**Aidan A. Bradley** - Creator and Maintainer

---

*libwsv5 is a community-supported project. Contributions and feedback are welcome.*

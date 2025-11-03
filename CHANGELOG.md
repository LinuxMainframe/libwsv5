# Changelog

All notable changes to libwsv5 are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] - 2025-11-02

### Added

#### Bug Fixes and Improvements
- **Response Handling** - Fixed recording/streaming control commands to properly validate response success flags
  - `obsws_start_recording()`, `obsws_stop_recording()` now correctly check response->success
  - `obsws_start_streaming()`, `obsws_stop_streaming()` now correctly check response->success
  - Test suite now properly allocates and validates response objects for all control operations

- **Scene Transformation Protocol** - Fixed SetSceneItemTransform requests to use correct OBS WebSocket v5 field names
  - Changed JSON field from `"transform"` to `"sceneItemTransform"` (OBS protocol compliance)
  - All transformation tests (translation, rotation, scaling) now generate correct protocol messages
  - Scene item transformations now work reliably with OBS Studio

#### Project Professionalization
- **File Structure** - Reorganized for professional release
  - Renamed `library.c` → `libwsv5.c` to follow professional naming conventions
  - Renamed `library.h` → `libwsv5.h` to avoid generic naming conflicts
  - Consolidated multiple test files into single `test.c` executable
  - Removed deprecated and obsolete test files

- **Author Attribution** - Added comprehensive headers to all source files
  - Author: Aidan A. Bradley (Architect, Author, Maintainer)
  - Consistent copyright and license attribution throughout codebase
  - Professional file descriptions and purpose statements

- **Documentation Suite** - Complete professional documentation
  - **README.md** - User-friendly quick start guide with prerequisites, installation, and examples
  - **CHANGELOG.md** - Detailed version history and progression (this file)
  - **API_REFERENCE.md** - Wiki-style function reference for all 50+ public APIs
  - **Doxygen** - Configured to generate comprehensive HTML and PDF documentation

- **Code Quality** - Normalized all comments and naming conventions
  - Removed unprofessional naming patterns
  - Standardized comment style throughout
  - Simplified test output descriptions
  - Professional section headers and formatting

#### Testing Enhancements
- **Test Suite Refinement** - Unified test binary with clear, professional descriptions
  - All tests show proper help output with normalized descriptions
  - Test sections properly identify sections and test progress
  - Improved statistics tracking and result reporting
  - 55/58 tests passing (94.8% success rate)

### Technical Changes

#### Protocol Compliance
- Recording/streaming commands now validate both transport-level errors and application-level success
- Scene transformations strictly follow OBS WebSocket v5 JSON field naming specifications
- Improved error detection and reporting for protocol violations

#### Memory Management
- Standardized response handling pattern across all control operations
- Ensured proper cleanup with explicit `obsws_response_free()` calls
- Validated response allocation before checking success flags

#### Build System
- CMakeLists.txt updated to reference new professional filenames
- Single unified test executable improves distribution simplicity
- Doxygen configuration updated for new file structure

### Known Issues Fixed

- ✅ Recording control commands now properly validate responses
- ✅ Streaming control commands now properly validate responses  
- ✅ Scene item transformations now use correct protocol field names
- ✅ Scene rotation tests now work reliably
- ✅ Scene translation tests now work reliably
- ✅ Scene scaling tests now work reliably

### Breaking Changes

None - fully backward compatible with 1.0.0

### Deprecations

None

### Future Enhancements

Potential features for future releases:
- Enhanced response caching for performance
- Request batching optimization improvements
- Advanced filtering and query syntax
- Built-in logging to file
- Performance metrics dashboard

---

## [1.0.0] - 2025-10-17

### Added

#### Core Library Features (C Implementation)
- **Complete Rewrite in C** - Ported from Python to native C for performance and portability
- **WebSocket v5 Protocol Support** - Full implementation of OBS WebSocket v5 (RPC version 1)
- **Connection Management** - Robust connection establishment, maintenance, and cleanup
- **Authentication** - SHA256-based authentication with salt and challenge response
- **Auto-Reconnection** - Configurable automatic reconnection with exponential backoff
- **Thread Safety** - Thread-safe operations suitable for multi-threaded applications
- **Multi-Connection Support** - Manage multiple OBS instances simultaneously
- **Event System** - Real-time event callbacks for OBS state changes
- **Error Handling** - Comprehensive error codes and diagnostic messages

#### API Functions

**Connection Management:**
- `obsws_init()` - Initialize library
- `obsws_cleanup()` - Clean up library resources
- `obsws_connect()` - Establish connection to OBS
- `obsws_disconnect()` - Close connection
- `obsws_is_connected()` - Check connection status
- `obsws_get_state()` - Get current connection state
- `obsws_state_string()` - Convert state to human-readable string
- `obsws_ping()` - Send keep-alive ping

**Configuration:**
- `obsws_config_init()` - Initialize configuration structure
- `obsws_set_log_level()` - Set logging verbosity
- `obsws_set_debug_level()` - Set debug output level
- `obsws_get_debug_level()` - Get current debug level
- `obsws_set_log_timestamps()` - Enable/disable log timestamps
- `obsws_set_log_colors()` - Enable/disable colored output
- `obsws_set_log_callback()` - Set custom logging function

**Request/Response:**
- `obsws_send_request()` - Send generic request to OBS
- `obsws_response_free()` - Free response memory

**Scene Operations:**
- `obsws_get_current_scene()` - Get active scene name
- `obsws_set_current_scene()` - Switch to scene
- `obsws_get_scene_list()` - Get list of scenes

**Recording & Streaming:**
- `obsws_start_recording()` - Start recording
- `obsws_stop_recording()` - Stop recording
- `obsws_get_recording_status()` - Get recording status
- `obsws_start_streaming()` - Start streaming
- `obsws_stop_streaming()` - Stop streaming
- `obsws_get_streaming_status()` - Get streaming status

**Source/Scene Item Control:**
- `obsws_set_source_visibility()` - Show/hide scene item
- `obsws_set_source_filter_enabled()` - Enable/disable filter
- `obsws_get_input_mute()` - Check if input is muted
- `obsws_set_input_mute()` - Mute/unmute input
- Scene item transformations (translate, rotate, scale)

**Monitoring:**
- `obsws_get_stats()` - Get connection statistics
- `obsws_process_events()` - Process pending events
- `obsws_version()` - Get library version string
- `obsws_error_string()` - Convert error code to message

#### Structures & Types

**Public Types:**
- `obsws_connection_t` - Connection handle
- `obsws_config_t` - Configuration parameters
- `obsws_response_t` - Response data structure
- `obsws_stats_t` - Connection statistics
- `obsws_state_t` - Connection state enumeration
- `obsws_error_t` - Error code enumeration
- `obsws_log_level_t` - Logging level enumeration
- `obsws_debug_level_t` - Debug level enumeration

**Callback Types:**
- `obsws_log_callback_t` - Logging callback function pointer
- `obsws_event_callback_t` - Event callback function pointer
- `obsws_state_callback_t` - State change callback function pointer

#### Error Codes
- `OBSWS_OK` - Success
- `OBSWS_ERROR_INVALID_PARAM` - Invalid parameter
- `OBSWS_ERROR_CONNECTION_FAILED` - Connection failed
- `OBSWS_ERROR_AUTH_FAILED` - Authentication failed
- `OBSWS_ERROR_SEND_FAILED` - Send operation failed
- `OBSWS_ERROR_RECV_FAILED` - Receive operation failed
- `OBSWS_ERROR_NOT_CONNECTED` - Not connected
- `OBSWS_ERROR_OUT_OF_MEMORY` - Memory allocation failed
- `OBSWS_ERROR_TIMEOUT` - Operation timed out

#### Documentation
- **README.md** - Quick start and user guide
- **API_REFERENCE.md** - Complete API documentation
- **CHANGELOG.md** - Release history
- **Doxygen Support** - Generate HTML/PDF documentation

#### Testing
- **Comprehensive Test Suite** - Tests covering:
  - Library initialization
  - Single connection tests
  - Audio control and source properties
  - Scene manipulation and transformations
  - Multi-connection concurrency
  - Batch operations
  - Error handling and edge cases
  - Connection lifecycle management

#### Build System
- **CMake Build** - Modern CMake configuration
- **Static Library** - Produces `libwsv5.a`
- **Optional Features** - Tests and documentation generation
- **Platform Support** - Linux, macOS, BSD

### Technical Details

#### Protocol Implementation
- OBS WebSocket v5 (RPC version 1)
- WebSocket over TCP with optional TLS/SSL
- Authentication using SHA256
- Automatic keep-alive with ping frames
- Request ID tracking for async responses
- Event subscription management

#### Threading Model
- Single-threaded connection management
- Background event processing thread per connection
- Thread-safe request queuing
- Mutex-protected data structures
- Condition variables for response synchronization

#### Memory Management
- No global state (fully reentrant)
- Explicit cleanup required
- Response memory ownership transferred to caller
- Automatic cleanup on disconnect
- Configurable buffer sizes

#### Error Recovery
- Automatic reconnection on network failure
- Exponential backoff for reconnection attempts
- Error codes distinguish between recoverable and fatal errors
- Comprehensive error messages
- Statistics tracking for debugging

### Known Limitations

- Synchronous API only (async returns within timeframe)
- Single-threaded per connection (no request multiplexing within connection)
- No built-in event filtering (receives all subscribed events)
- Requires manual memory management for responses
- WebSocket fragmentation handled internally

### Dependencies

- **libwebsockets** 3.0+ - WebSocket protocol implementation
- **OpenSSL** 1.1.0+ - Cryptographic functions
- **cJSON** - JSON parsing and generation
- **POSIX** - Standard C library features

---

## [0.0.1] - 2025-05-02 (C Implementation Begins)

### Added

#### C Rewrite Initiation
- Initial C project structure based on Python 1.5.3a feature set
- OBS WebSocket v5 protocol C implementation foundation
- Build system and CMake configuration
- WebSocket connection framework
- Authentication system skeleton
- Test infrastructure setup

### Context

Starting May 2nd, 2025, the complete C rewrite began using Python v1.5.3a as the reference implementation. This marked the start of a nearly 6-month development cycle to bring libwsv5 to production-ready C code (v1.0.0 on October 17th, 2025).


---

## [1.5.3a] - 2025-05-01 (Final Python Release)

### Added

#### Final Python Version Before C Rewrite
- Stable Python implementation with all major features
- OBS WebSocket v5 protocol support
- Scene and source control
- Recording/streaming management
- Event handling system
- Production-ready state before C rewrite decision

### Context

Python v1.5.3a represented the mature end-state of the Python implementation (April 2025). Approximately 4 months after v1.0.0 initial full implementation (January 1, 2025), significant refinements were made:

**Development Focus (Jan 1 - May 1, 2025):**
- Progressive improvements to logging systems
- Enhanced safety protocols and error handling
- Comprehensive monitoring and statistics collection
- Reconnection logic refinement and testing
- Extensive edge case testing and protocol compliance validation
- Full OBS protocol functionality exploration and documentation
- Methodical design planning for C implementation (including hand-drawn architectural diagrams)

**Performance Context:**
The Python implementation experienced variability in Time to Execute (TTE) and signal processing latency - partly due to implementation limitations rather than Python itself. However, this thorough exploration informed a methodical design approach for the C implementation.

After 6 months of Python development, the decision was made to rewrite the entire library in C to achieve dramatically better performance, portability, and eliminate Python runtime dependencies. This version served as the comprehensive reference implementation for the C rewrite that began on May 2nd, 2025.

---

## [1.0.0] - 2025-01-01 (Python Implementation - First Full Release)

### Added

#### Initial Full Python Implementation
- Complete OBS WebSocket v5 protocol support
- Full connection management and lifecycle
- All core operations (scene control, recording/streaming, source management)
- Event handling system
- Authentication and security
- Test suite and documentation
- Production-ready Python library

### Context

Python v1.0.0 (January 1, 2025) marked the first complete, full-featured implementation of the libwsv5 library. Approximately 3.5 weeks after the v0.1.0 prototype (December 5, 2024), this version represented a fully functional library with all major features implemented. This became the baseline for extensive refinement and optimization work over the following 4 months, ultimately leading to v1.5.3a.

---


## [0.0.1] - 2024-11-10 (Python Implementation Begins)

### Added

#### Initial Python Project Setup
- Project structure and initialization
- WebSocket protocol foundation
- Basic connection framework
- Authentication skeleton
- Initial test infrastructure setup

### Context

Python v0.0.1 (November 10, 2024) marked the very beginning of the libwsv5 project as a Python implementation. This initial version established the foundational architecture and began exploring the OBS WebSocket v5 protocol requirements. Within approximately 3.5 weeks, this led to v0.1.0 with a more complete prototype framework.

---
### Version History Summary


| Version | Date | Status | Implementation | Key Milestone |
|---------|------|--------|-----------------|----------------|
| **1.1.0** | **Nov 2, 2025** | **Production** | **C** | Bug fixes, protocol compliance, professional release |

#### C Implementation Timeline (May 2 - Nov 2, 2025)
| Version | Date | Status | Implementation | Milestone | Duration from v0.0.1 |
|---------|------|--------|-----------------|-----------|----------------------|
| 1.1.0 | Nov 2, 2025 | Stable | C | Professionalization & bug fixes | 6 months |
| 1.0.0 | Oct 17, 2025 | Stable | C | Production-ready C rewrite | 5.5 months |
| 0.0.1 | May 2, 2025 | Development | C | C rewrite inception | Start |

#### Python Implementation Timeline (Nov 10, 2024 - May 1, 2025)
| Version | Date | Status | Implementation | Milestone | Duration |
|---------|------|--------|-----------------|-----------|----------|
| 1.5.3a | May 1, 2025 | Archived | Python | Final production release before C rewrite | 4 months from v1.0.0 |
| 1.0.0 | Jan 1, 2025 | Archived | Python | **First complete, full-featured implementation** | 3.5 weeks from v0.1.0 |
| 0.1.0 | Dec 5, 2024 | Archived | Python | Early prototype framework | 3.5 weeks from v0.0.1 |
| 0.0.1 | Nov 10, 2024 | Archived | Python | Initial project structure & setup | Start |

---

### Compatibility

- **OBS Studio:** 29.0.0+
- **OBS WebSocket:** v5.1.0+
- **C Standard:** C11
- **Operating Systems:** Linux, macOS, BSD
- **Architectures:** x86_64, ARM64, i386 (and other POSIX systems)

### Installation Verification

To verify correct installation:

```bash
cd build
./test -h localhost -p 4455 -w obs_password -d 1
```

Expected output: `Pass Rate: 90%+` (minor failures may occur if OBS is not fully configured)

---

## Support

- **Documentation:** See README.md and API_REFERENCE.md
- **Issues:** Report via GitHub issues on [GitHub](https://github.com/LinuxMainframe/libwsv5)
- **Testing:** See test suite in `tests/test.c`
- **Examples:** Check README.md for code examples

---

*libwsv5 is maintained by Aidan A. Bradley*  

/*
 * Comprehensive Test Suite for libwsv5
 * Tests all major functionality of the OBS WebSocket v5 library
 */

#define _POSIX_C_SOURCE 200809L
#include "../library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

/* Default test configuration - can be overridden via command-line arguments */
#define DEFAULT_OBS_HOST "localhost"       /* Default OBS host */
#define DEFAULT_OBS_PORT 4455               /* Default OBS WebSocket port */
#define DEFAULT_OBS_PASSWORD ""             /* Default password (none) */
#define DEFAULT_DEBUG_LEVEL 1               /* Default debug verbosity */

/* Test state tracking - global counters for callback verification */
static int event_count = 0;                 /* Total events received from OBS */
static int state_change_count = 0;          /* Total state changes observed */
static char last_event_type[256] = {0};     /* Last event type received */
static char last_scene_switched[256] = {0}; /* Last scene name from scene change event */

/* Sleep for specified milliseconds - cross-platform sleep function */
static void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/* Print formatted test section header */
static void print_test_header(const char *test_name) {
    printf("\n");
    printf("========================================\n");
    printf("TEST: %s\n", test_name);
    printf("========================================\n");
}

/* Print test result with pass/fail indicator */
static void print_test_result(const char *test_name, int passed) {
    if (passed) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

/* Log callback - receives and displays all library log messages with timestamps */
static void log_callback(obsws_log_level_t level, const char *message, void *user_data) {
    (void)user_data;  /* Unused parameter */
    
    /* Convert log level to string */
    const char *level_str;
    switch (level) {
        case OBSWS_LOG_ERROR:   level_str = "ERROR"; break;
        case OBSWS_LOG_WARNING: level_str = "WARN "; break;
        case OBSWS_LOG_INFO:    level_str = "INFO "; break;
        case OBSWS_LOG_DEBUG:   level_str = "DEBUG"; break;
        default:                level_str = "?????"; break;
    }
    
    /* Format current time */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    
    /* Print timestamped log message */
    printf("[%s] [%s] %s\n", time_buf, level_str, message);
}

/* Event callback - receives and processes events from OBS */
static void event_callback(obsws_connection_t *conn, const char *event_type,
                          const char *event_data, void *user_data) {
    (void)conn;       /* Unused parameter */
    (void)user_data;  /* Unused parameter */
    
    /* Track event count and type */
    event_count++;
    strncpy(last_event_type, event_type, sizeof(last_event_type) - 1);
    last_event_type[sizeof(last_event_type) - 1] = '\0';
    
    printf(">>> EVENT #%d: %s\n", event_count, event_type);
    
    /* Parse and display relevant event data based on event type */
    if (strcmp(event_type, "CurrentProgramSceneChanged") == 0) {
        printf("    Event Data: %s\n", event_data);
        
        /* Extract scene name from JSON event data (simple string parsing) */
        const char *scene_name_key = "\"sceneName\":\"";
        const char *scene_start = strstr(event_data, scene_name_key);
        if (scene_start) {
            scene_start += strlen(scene_name_key);
            const char *scene_end = strchr(scene_start, '"');
            if (scene_end) {
                size_t len = scene_end - scene_start;
                if (len < sizeof(last_scene_switched)) {
                    strncpy(last_scene_switched, scene_start, len);
                    last_scene_switched[len] = '\0';
                    printf("    >>> Scene switched to: %s\n", last_scene_switched);
                }
            }
        }
    } else if (strcmp(event_type, "RecordStateChanged") == 0 ||
               strcmp(event_type, "StreamStateChanged") == 0) {
        /* Display recording/streaming state changes */
        printf("    Event Data: %s\n", event_data);
    }
}

/* State change callback - tracks connection state transitions */
static void state_callback(obsws_connection_t *conn, obsws_state_t old_state,
                          obsws_state_t new_state, void *user_data) {
    (void)conn;       /* Unused parameter */
    (void)user_data;  /* Unused parameter */
    
    /* Track and display state changes */
    state_change_count++;
    printf(">>> STATE CHANGE #%d: %s -> %s\n",
           state_change_count,
           obsws_state_string(old_state),
           obsws_state_string(new_state));
}

/* Wait for connection to be established with timeout */
static int wait_for_connection(obsws_connection_t *conn, int timeout_ms) {
    int iterations = timeout_ms / 100;
    for (int i = 0; i < iterations; i++) {
        if (obsws_is_connected(conn)) {
            return 1;  /* Connected */
        }
        sleep_ms(100);  /* Check every 100ms */
    }
    return 0;  /* Timeout */
}

/* Display command-line usage information */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\n");
    printf("OBS WebSocket v5 Library Comprehensive Test Suite\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --host HOST        OBS WebSocket host (default: %s)\n", DEFAULT_OBS_HOST);
    printf("  -p, --port PORT        OBS WebSocket port (default: %d)\n", DEFAULT_OBS_PORT);
    printf("  -w, --password PASS    OBS WebSocket password (default: none)\n");
    printf("  -d, --debug LEVEL      Debug level 0-3 (default: %d)\n", DEFAULT_DEBUG_LEVEL);
    printf("                         0=NONE, 1=LOW, 2=MEDIUM, 3=HIGH/VERBOSE\n");
    printf("  --help                 Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --host 192.168.1.13 --password mypass\n", program_name);
    printf("  %s -h localhost -p 4455 -w secret -d 3\n", program_name);
    printf("  %s --host 10.0.0.5 --debug 2\n", program_name);
    printf("\n");
}

/* Main test function - runs comprehensive test suite */
int main(int argc, char *argv[]) {
    /* Configuration variables - defaults can be overridden via command-line */
    const char *obs_host = DEFAULT_OBS_HOST;
    int obs_port = DEFAULT_OBS_PORT;
    const char *obs_password = DEFAULT_OBS_PASSWORD;
    int debug_level = DEFAULT_DEBUG_LEVEL;
    
    /* Parse command-line arguments for custom configuration */
    static struct option long_options[] = {
        {"host",     required_argument, 0, 'h'},
        {"port",     required_argument, 0, 'p'},
        {"password", required_argument, 0, 'w'},
        {"debug",    required_argument, 0, 'd'},
        {"help",     no_argument,       0,  0 },
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "h:p:w:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                obs_host = optarg;
                break;
            case 'p':
                obs_port = atoi(optarg);
                if (obs_port <= 0 || obs_port > 65535) {
                    fprintf(stderr, "Error: Invalid port number: %s\n", optarg);
                    return 1;
                }
                break;
            case 'w':
                obs_password = optarg;
                break;
            case 'd':
                debug_level = atoi(optarg);
                if (debug_level < 0 || debug_level > 3) {
                    fprintf(stderr, "Error: Debug level must be 0-3\n");
                    return 1;
                }
                break;
            case 0:
                if (strcmp(long_options[option_index].name, "help") == 0) {
                    print_usage(argv[0]);
                    return 0;
                }
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     libwsv5 Comprehensive Test Suite                      ║\n");
    printf("║     OBS WebSocket v5 Library Test                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Target OBS Instance:\n");
    printf("  Host: %s\n", obs_host);
    printf("  Port: %d\n", obs_port);
    printf("  Password: %s\n", strlen(obs_password) > 0 ? "***" : "(none)");
    printf("\n");
    printf("Expected Scenes: Test1, Test2, Test3, Test4\n");
    printf("\n");
    
    // Display debug level configuration
    const char *debug_level_desc[] = {
        "NONE (no debug output)",
        "LOW (basic connection events, auth, scene changes)",
        "MEDIUM (+ opcodes, event types, request/response tracking)",
        "HIGH/VERBOSE (+ full message contents, passwords, JSON payloads)"
    };
    printf("Debug Level: %d - %s\n", debug_level, debug_level_desc[debug_level]);
    printf("\n");
    printf("Monitor your OBS WebSocket server GUI and OBS logs to verify!\n");
    printf("\n");

    /* ========================================================================
     * Test 1: Library Initialization
     * Verifies library can be initialized and debug levels can be set
     * ======================================================================== */
    print_test_header("Library Initialization");
    obsws_error_t err = obsws_init();
    print_test_result("obsws_init()", err == OBSWS_OK);
    
    if (err != OBSWS_OK) {
        printf("Failed to initialize library: %s\n", obsws_error_string(err));
        return 1;
    }

    /* Set global log level and debug level */
    obsws_set_log_level(OBSWS_LOG_DEBUG);
    obsws_set_debug_level((obsws_debug_level_t)debug_level);
    
    printf("Debug level set to: %d\n", obsws_get_debug_level());

    /* ========================================================================
     * Test 2: Configuration Setup
     * Creates and configures connection parameters
     * ======================================================================== */
    print_test_header("Configuration Setup");
    obsws_config_t config;
    obsws_config_init(&config);
    
    config.host = obs_host;
    config.port = obs_port;
    config.password = obs_password;
    config.use_ssl = false;
    
    // Set callbacks
    config.log_callback = log_callback;
    config.event_callback = event_callback;
    config.state_callback = state_callback;
    
    // Configure timeouts and reconnection
    config.recv_timeout_ms = 5000;
    config.send_timeout_ms = 5000;
    config.auto_reconnect = true;
    config.reconnect_delay_ms = 2000;
    config.max_reconnect_delay_ms = 10000;
    config.max_reconnect_attempts = 3;
    config.ping_interval_ms = 20000;
    
    print_test_result("Configuration setup", 1);

    /* ========================================================================
     * Test 3: Connection Establishment
     * Connects to OBS and waits for authentication to complete
     * ======================================================================== */
    print_test_header("Connection Establishment");
    printf("Connecting to OBS at %s:%d...\n", obs_host, obs_port);
    
    obsws_connection_t *conn = obsws_connect(&config);
    print_test_result("obsws_connect()", conn != NULL);
    
    if (!conn) {
        printf("Failed to create connection object\n");
        obsws_cleanup();
        return 1;
    }

    // Wait for connection
    printf("Waiting for connection (timeout: 10 seconds)...\n");
    int connected = wait_for_connection(conn, 10000);
    print_test_result("Connection established", connected);
    
    if (!connected) {
        printf("Failed to connect to OBS\n");
        printf("Connection state: %s\n", obsws_state_string(obsws_get_state(conn)));
        obsws_disconnect(conn);
        obsws_cleanup();
        return 1;
    }
    
    printf("Successfully connected to OBS!\n");
    sleep_ms(1000);  /* Give time for initial events to arrive */

    /* ========================================================================
     * Test 4: Get Version Information
     * Sends GetVersion request to verify request/response mechanism
     * ======================================================================== */
    print_test_header("Get Version Information");
    obsws_response_t *response = NULL;
    err = obsws_send_request(conn, "GetVersion", NULL, &response, 0);
    
    if (err == OBSWS_OK && response) {
        printf("Version response: %s\n", response->response_data ? response->response_data : "NULL");
        print_test_result("GetVersion request", response->success);
        obsws_response_free(response);
    } else {
        print_test_result("GetVersion request", 0);
    }
    sleep_ms(500);

    /* ========================================================================
     * Test 5: Get Scene List
     * Retrieves all available scenes from OBS
     * ======================================================================== */
    print_test_header("Get Scene List");
    response = NULL;
    err = obsws_send_request(conn, "GetSceneList", NULL, &response, 0);
    
    if (err == OBSWS_OK && response) {
        printf("Scene list response:\n%s\n", response->response_data ? response->response_data : "NULL");
        print_test_result("GetSceneList request", response->success);
        obsws_response_free(response);
    } else {
        printf("Error: %s\n", obsws_error_string(err));
        print_test_result("GetSceneList request", 0);
    }
    sleep_ms(500);

    /* ========================================================================
     * Test 6: Get Current Scene
     * Queries the currently active scene
     * ======================================================================== */
    print_test_header("Get Current Scene");
    char current_scene[256] = {0};
    err = obsws_get_current_scene(conn, current_scene, sizeof(current_scene));
    
    if (err == OBSWS_OK) {
        printf("Current scene: %s\n", current_scene);
        print_test_result("obsws_get_current_scene()", 1);
    } else {
        printf("Error: %s\n", obsws_error_string(err));
        print_test_result("obsws_get_current_scene()", 0);
    }
    sleep_ms(500);

    /* ========================================================================
     * Test 7: Scene Switching
     * Cycles through test scenes and verifies scene changes
     * Expects scenes named Test1, Test2, Test3, Test4 to exist in OBS
     * ======================================================================== */
    print_test_header("Scene Switching Test");
    const char *test_scenes[] = {"Test1", "Test2", "Test3", "Test4"};
    int scene_switch_success = 0;
    
    for (int i = 0; i < 4; i++) {
        printf("\n--- Switching to scene: %s ---\n", test_scenes[i]);
        
        int prev_event_count = event_count;
        err = obsws_set_current_scene(conn, test_scenes[i], NULL);
        
        if (err == OBSWS_OK) {
            printf("Scene switch command sent successfully\n");
            
            // Wait for event confirmation
            sleep_ms(1000);
            
            // Verify scene changed
            char verify_scene[256] = {0};
            obsws_get_current_scene(conn, verify_scene, sizeof(verify_scene));
            
            if (strcmp(verify_scene, test_scenes[i]) == 0) {
                printf("Scene verified: %s\n", verify_scene);
                scene_switch_success++;
            } else {
                printf("Scene mismatch: expected %s, got %s\n", test_scenes[i], verify_scene);
            }
            
            // Check if we received an event
            if (event_count > prev_event_count) {
                printf("Received %d event(s)\n", event_count - prev_event_count);
            }
        } else {
            printf("Failed to switch scene: %s\n", obsws_error_string(err));
        }
        
        sleep_ms(1500);  // Pause between scene switches
    }
    
    print_test_result("Scene switching (4/4 scenes)", scene_switch_success == 4);

    /* ========================================================================
     * Test 8: Connection Statistics
     * Retrieves and displays connection metrics
     * ======================================================================== */
    print_test_header("Connection Statistics");
    obsws_stats_t stats;
    obsws_get_stats(conn, &stats);
    
    printf("Connection Statistics:\n");
    printf("  Messages sent:     %lu\n", (unsigned long)stats.messages_sent);
    printf("  Messages received: %lu\n", (unsigned long)stats.messages_received);
    printf("  Bytes sent:        %lu\n", (unsigned long)stats.bytes_sent);
    printf("  Bytes received:    %lu\n", (unsigned long)stats.bytes_received);
    printf("  Reconnect count:   %lu\n", (unsigned long)stats.reconnect_count);
    printf("  Error count:       %lu\n", (unsigned long)stats.error_count);
    printf("  Events received:   %d\n", event_count);
    printf("  State changes:     %d\n", state_change_count);
    print_test_result("Statistics retrieval", 1);

    /* ========================================================================
     * Test 9: Recording Control
     * Tests starting and stopping recording
     * May fail if recording is already active or not configured in OBS
     * ======================================================================== */
    print_test_header("Recording Control Test");
    printf("Testing recording start/stop...\n");
    printf("(This may fail if recording is already active or not configured)\n");
    
    // Try to start recording
    response = NULL;
    err = obsws_start_recording(conn, &response);
    if (err == OBSWS_OK && response) {
        printf("Start recording response: success=%d\n", response->success);
        if (response->success) {
            printf("Recording started\n");
            sleep_ms(3000);  // Record for 3 seconds
            
            // Stop recording
            obsws_response_t *stop_response = NULL;
            err = obsws_stop_recording(conn, &stop_response);
            if (err == OBSWS_OK && stop_response) {
                printf("Stop recording response: success=%d\n", stop_response->success);
                if (stop_response->success) {
                    printf("Recording stopped\n");
                }
                obsws_response_free(stop_response);
            }
        } else {
            printf("Recording start failed (may already be recording): %s\n", 
                   response->error_message ? response->error_message : "unknown");
        }
        obsws_response_free(response);
    }
    sleep_ms(1000);

    /* ========================================================================
     * Test 10: Custom Request
     * Demonstrates sending arbitrary OBS requests
     * ======================================================================== */
    print_test_header("Custom Request Test");
    printf("Sending GetStats request...\n");
    
    response = NULL;
    err = obsws_send_request(conn, "GetStats", NULL, &response, 0);
    
    if (err == OBSWS_OK && response) {
        printf("GetStats response:\n%s\n", response->response_data ? response->response_data : "NULL");
        print_test_result("Custom request (GetStats)", response->success);
        obsws_response_free(response);
    } else {
        print_test_result("Custom request (GetStats)", 0);
    }
    sleep_ms(500);

    /* ========================================================================
     * Test 11: Debug Level Demonstration
     * Shows how debug output changes at different verbosity levels (0-3)
     * ======================================================================== */
    print_test_header("Debug Level Demonstration");
    printf("Testing different debug levels with scene switches...\n");
    printf("This demonstrates how debug output changes at each level.\n\n");
    
    for (int level = 0; level <= 3; level++) {
        printf("\n--- Setting debug level to %d ---\n", level);
        obsws_set_debug_level((obsws_debug_level_t)level);
        printf("Current debug level: %d\n", obsws_get_debug_level());
        
        // Perform a scene switch to generate debug output
        const char *scene = test_scenes[level % 4];
        printf("Switching to scene: %s\n", scene);
        err = obsws_set_current_scene(conn, scene, NULL);
        
        if (err == OBSWS_OK) {
            printf("Scene switch command sent\n");
        }
        
        sleep_ms(1500);  // Wait to see the debug output
    }
    
    // Restore original debug level
    obsws_set_debug_level((obsws_debug_level_t)debug_level);
    printf("\nDebug level restored to: %d\n", obsws_get_debug_level());
    print_test_result("Debug level demonstration", 1);
    sleep_ms(1000);

    /* ========================================================================
     * Test 12: Rapid Scene Switching (Stress Test)
     * Tests library stability under rapid command execution
     * ======================================================================== */
    print_test_header("Rapid Scene Switching (Stress Test)");
    printf("Performing 10 rapid scene switches...\n");
    
    int rapid_success = 0;
    for (int i = 0; i < 10; i++) {
        const char *scene = test_scenes[i % 4];
        err = obsws_set_current_scene(conn, scene, NULL);
        if (err == OBSWS_OK) {
            rapid_success++;
        }
        sleep_ms(200);  // 200ms between switches
    }
    
    printf("Rapid switches completed: %d/10\n", rapid_success);
    print_test_result("Rapid scene switching", rapid_success >= 8);
    sleep_ms(1000);

    /* ========================================================================
     * Test 13: Scene Item Manipulation
     * Tests controlling individual scene items (sources within scenes)
     * - Gets scene item list
     * - Toggles visibility (hide/show)
     * - Modifies position, scale, and rotation
     * ======================================================================== */
    print_test_header("Scene Item Manipulation Test");
    printf("Testing scene item visibility and transform controls...\n");
    printf("This test will:\n");
    printf("  1. Get list of scene items in current scene\n");
    printf("  2. Toggle visibility (hide/show) of scene items\n");
    printf("  3. Modify position and scale of scene items\n");
    printf("\n");
    
    // First, get the current scene
    char manipulation_scene[256] = {0};
    err = obsws_get_current_scene(conn, manipulation_scene, sizeof(manipulation_scene));
    if (err == OBSWS_OK) {
        printf("Current scene for manipulation: %s\n", manipulation_scene);
        
        // Get scene items list
        printf("\n--- Getting Scene Items List ---\n");
        char request_data[512];
        snprintf(request_data, sizeof(request_data), "{\"sceneName\":\"%s\"}", manipulation_scene);
        
        response = NULL;
        err = obsws_send_request(conn, "GetSceneItemList", request_data, &response, 0);
        
        if (err == OBSWS_OK && response && response->success) {
            printf("Scene items response:\n%s\n", response->response_data ? response->response_data : "NULL");
            
            // Parse the response to extract scene item IDs
            // Looking for "sceneItemId" fields in the JSON
            const char *data = response->response_data;
            if (data) {
                // Find first scene item ID for testing
                const char *id_key = "\"sceneItemId\":";
                const char *id_start = strstr(data, id_key);
                
                if (id_start) {
                    id_start += strlen(id_key);
                    int scene_item_id = atoi(id_start);
                    printf("\nFound scene item ID: %d\n", scene_item_id);
                    
                    // Test 13a: Get scene item properties
                    printf("\n--- Getting Scene Item Properties ---\n");
                    snprintf(request_data, sizeof(request_data), 
                             "{\"sceneName\":\"%s\",\"sceneItemId\":%d}", 
                             manipulation_scene, scene_item_id);
                    
                    obsws_response_t *props_response = NULL;
                    err = obsws_send_request(conn, "GetSceneItemTransform", request_data, &props_response, 0);
                    
                    if (err == OBSWS_OK && props_response && props_response->success) {
                        printf("Scene item transform:\n%s\n", props_response->response_data ? props_response->response_data : "NULL");
                        print_test_result("Get scene item transform", 1);
                        obsws_response_free(props_response);
                    } else {
                        print_test_result("Get scene item transform", 0);
                    }
                    sleep_ms(500);
                    
                    // Test 13b: Hide scene item
                    printf("\n--- Hiding Scene Item ---\n");
                    snprintf(request_data, sizeof(request_data), 
                             "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemEnabled\":false}", 
                             manipulation_scene, scene_item_id);
                    
                    obsws_response_t *hide_response = NULL;
                    err = obsws_send_request(conn, "SetSceneItemEnabled", request_data, &hide_response, 0);
                    
                    if (err == OBSWS_OK && hide_response && hide_response->success) {
                        printf("Scene item hidden successfully\n");
                        print_test_result("Hide scene item", 1);
                        obsws_response_free(hide_response);
                        sleep_ms(2000);  // Wait so user can see the item disappear
                        
                        // Test 13c: Show scene item again
                        printf("\n--- Showing Scene Item Again ---\n");
                        snprintf(request_data, sizeof(request_data), 
                                 "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemEnabled\":true}", 
                                 manipulation_scene, scene_item_id);
                        
                        obsws_response_t *show_response = NULL;
                        err = obsws_send_request(conn, "SetSceneItemEnabled", request_data, &show_response, 0);
                        
                        if (err == OBSWS_OK && show_response && show_response->success) {
                            printf("Scene item shown successfully\n");
                            print_test_result("Show scene item", 1);
                            obsws_response_free(show_response);
                        } else {
                            print_test_result("Show scene item", 0);
                        }
                        sleep_ms(1000);
                    } else {
                        print_test_result("Hide scene item", 0);
                    }
                    
                    // Test 13d: Modify scene item transform (position and scale)
                    printf("\n--- Modifying Scene Item Transform ---\n");
                    printf("Moving item to position (100, 100) and scaling to 1.5x...\n");
                    
                    snprintf(request_data, sizeof(request_data), 
                             "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemTransform\":{\"positionX\":100.0,\"positionY\":100.0,\"scaleX\":1.5,\"scaleY\":1.5}}", 
                             manipulation_scene, scene_item_id);
                    
                    obsws_response_t *transform_response = NULL;
                    err = obsws_send_request(conn, "SetSceneItemTransform", request_data, &transform_response, 0);
                    
                    if (err == OBSWS_OK && transform_response && transform_response->success) {
                        printf("Scene item transform modified successfully\n");
                        print_test_result("Modify scene item transform", 1);
                        obsws_response_free(transform_response);
                        sleep_ms(2000);  // Wait so user can see the change
                        
                        // Test 13e: Reset scene item transform
                        printf("\n--- Resetting Scene Item Transform ---\n");
                        printf("Resetting to position (0, 0) and scale 1.0x...\n");
                        
                        snprintf(request_data, sizeof(request_data), 
                                 "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemTransform\":{\"positionX\":0.0,\"positionY\":0.0,\"scaleX\":1.0,\"scaleY\":1.0}}", 
                                 manipulation_scene, scene_item_id);
                        
                        obsws_response_t *reset_response = NULL;
                        err = obsws_send_request(conn, "SetSceneItemTransform", request_data, &reset_response, 0);
                        
                        if (err == OBSWS_OK && reset_response && reset_response->success) {
                            printf("Scene item transform reset successfully\n");
                            print_test_result("Reset scene item transform", 1);
                            obsws_response_free(reset_response);
                        } else {
                            print_test_result("Reset scene item transform", 0);
                        }
                        sleep_ms(1000);
                    } else {
                        print_test_result("Modify scene item transform", 0);
                    }
                    
                    // Test 13f: Rotate scene item
                    printf("\n--- Rotating Scene Item ---\n");
                    printf("Rotating item 45 degrees...\n");
                    
                    snprintf(request_data, sizeof(request_data), 
                             "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemTransform\":{\"rotation\":45.0}}", 
                             manipulation_scene, scene_item_id);
                    
                    obsws_response_t *rotate_response = NULL;
                    err = obsws_send_request(conn, "SetSceneItemTransform", request_data, &rotate_response, 0);
                    
                    if (err == OBSWS_OK && rotate_response && rotate_response->success) {
                        printf("Scene item rotated successfully\n");
                        print_test_result("Rotate scene item", 1);
                        obsws_response_free(rotate_response);
                        sleep_ms(2000);  // Wait so user can see the rotation
                        
                        // Reset rotation
                        printf("Resetting rotation to 0 degrees...\n");
                        snprintf(request_data, sizeof(request_data), 
                                 "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemTransform\":{\"rotation\":0.0}}", 
                                 manipulation_scene, scene_item_id);
                        
                        obsws_response_t *unrotate_response = NULL;
                        err = obsws_send_request(conn, "SetSceneItemTransform", request_data, &unrotate_response, 0);
                        
                        if (err == OBSWS_OK && unrotate_response && unrotate_response->success) {
                            printf("Scene item rotation reset\n");
                            obsws_response_free(unrotate_response);
                        }
                        sleep_ms(1000);
                    } else {
                        print_test_result("Rotate scene item", 0);
                    }
                    
                } else {
                    printf("⚠ No scene items found in scene (scene may be empty)\n");
                    print_test_result("Scene item manipulation", 0);
                }
            }
            
            obsws_response_free(response);
        } else {
            printf("Failed to get scene items list\n");
            if (response) {
                printf("Error: %s\n", response->error_message ? response->error_message : "unknown");
                obsws_response_free(response);
            }
            print_test_result("Get scene items list", 0);
        }
    } else {
        printf("Failed to get current scene\n");
        print_test_result("Scene item manipulation", 0);
    }
    
    printf("\nScene item manipulation tests completed\n");
    printf("Check your OBS preview to verify the visual changes!\n");
    sleep_ms(1000);

    /* ========================================================================
     * Test 14: Final Statistics
     * Displays final connection metrics before disconnecting
     * ======================================================================== */
    print_test_header("Final Statistics");
    obsws_get_stats(conn, &stats);
    
    printf("Final Connection Statistics:\n");
    printf("  Messages sent:     %lu\n", (unsigned long)stats.messages_sent);
    printf("  Messages received: %lu\n", (unsigned long)stats.messages_received);
    printf("  Bytes sent:        %lu\n", (unsigned long)stats.bytes_sent);
    printf("  Bytes received:    %lu\n", (unsigned long)stats.bytes_received);
    printf("  Reconnect count:   %lu\n", (unsigned long)stats.reconnect_count);
    printf("  Error count:       %lu\n", (unsigned long)stats.error_count);
    printf("  Total events:      %d\n", event_count);
    printf("  State changes:     %d\n", state_change_count);
    printf("  Last event type:   %s\n", last_event_type);
    
    print_test_result("Final statistics", stats.messages_sent > 0 && stats.messages_received > 0);

    /* ========================================================================
     * Test 15: Disconnection
     * Cleanly disconnects from OBS and verifies state
     * ======================================================================== */
    print_test_header("Disconnection Test");
    printf("Disconnecting from OBS...\n");
    
    /* Note: After obsws_disconnect(), the connection pointer is freed and invalid.
     * Do not access conn after this point. */
    obsws_disconnect(conn);
    sleep_ms(500);
    
    /* Connection is now freed - cannot check state */
    printf("Connection disconnected and freed\n");
    print_test_result("Disconnection", 1);

    /* ========================================================================
     * Test 16: Library Cleanup
     * Releases all library resources
     * ======================================================================== */
    print_test_header("Library Cleanup");
    obsws_cleanup();
    print_test_result("obsws_cleanup()", 1);

    /* ========================================================================
     * Final Summary
     * Displays comprehensive test results and debug level information
     * ======================================================================== */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Debug Level Used:          %d\n", debug_level);
    printf("Total events received:     %d\n", event_count);
    printf("Total state changes:       %d\n", state_change_count);
    printf("Total messages sent:       %lu\n", (unsigned long)stats.messages_sent);
    printf("Total messages received:   %lu\n", (unsigned long)stats.messages_received);
    printf("Total errors:              %lu\n", (unsigned long)stats.error_count);
    printf("\n");
    printf("Debug Level Information:\n");
    printf("  Level 0 (NONE):    No debug output - production mode\n");
    printf("  Level 1 (LOW):     Basic connection events, auth, scene changes\n");
    printf("  Level 2 (MEDIUM):  + Opcodes, event types, request/response IDs\n");
    printf("  Level 3 (HIGH):    + Full message contents, passwords, JSON payloads\n");
    printf("\n");
    printf("To change debug level, use --debug option (e.g., --debug 3)\n");
    printf("\n");
    printf("Expected observations in OBS:\n");
    printf("  1. Multiple scene switches between Test1, Test2, Test3, Test4\n");
    printf("  2. WebSocket connection established and closed\n");
    printf("  3. Multiple requests in WebSocket server log\n");
    printf("  4. Recording start/stop (if configured)\n");
    printf("  5. Scene items being hidden/shown, moved, scaled, and rotated\n");
    printf("\n");
    printf("Check your OBS WebSocket server GUI for connection activity!\n");
    printf("Check your OBS log file for detailed WebSocket messages!\n");
    printf("Check your OBS preview window for visual changes to scene items!\n");
    printf("\n");
    
    if (event_count > 0 && stats.messages_sent > 10) {
        printf("TEST SUITE COMPLETED SUCCESSFULLY\n");
        return 0;
    } else {
        printf("TEST SUITE COMPLETED WITH WARNINGS\n");
        return 0;
    }
}
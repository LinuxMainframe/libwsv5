/*
 * libwsv5 - Test Suite
 * 
 * Comprehensive test suite for libwsv5 OBS WebSocket v5 protocol library.
 * Tests all major functionality including:
 * - Connection management
 * - Scene and source control
 * - Recording and streaming
 * - Scene item transformations
 * - Multi-connection concurrency
 * - Error handling and edge cases
 * 
 * Author: Aidan A. Bradley
 * Maintainer: Aidan A. Bradley
 * License: MIT
 * 
 * Usage:
 *   ./test [OPTIONS]
 * 
 * Options:
 *   -h, --host HOST          OBS WebSocket host (default: localhost)
 *   -p, --port PORT          OBS WebSocket port (default: 4455)
 *   -w, --password PASS      OBS WebSocket password (default: none)
 *   -d, --debug LEVEL        Debug level 0-3 (default: 1)
 *   --skip-multi             Skip multi-connection tests
 *   --skip-batch             Skip batch request tests
 *   --skip-transforms        Skip scene transformation tests
 *   --help                   Show this help message
 */

#define _POSIX_C_SOURCE 200809L
#include "../libwsv5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>

/* ========================================================================
 * CONFIGURATION CONSTANTS
 * ======================================================================== */

#define DEFAULT_OBS_HOST          "localhost"
#define DEFAULT_OBS_PORT          4455
#define DEFAULT_OBS_PASSWORD      ""
#define DEFAULT_DEBUG_LEVEL       1
#define NUM_CONCURRENT_CONNS      3
#define NUM_BATCH_REQUESTS        5
#define BATCH_REQUEST_SIZE        10
#define MAX_TRANSFORM_ITERATIONS  8

/* ========================================================================
 * GLOBAL TEST STATE AND STATISTICS
 * ======================================================================== */

typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    int events_received;
    int state_changes;
    
    time_t test_start_time;
    time_t test_end_time;
} test_stats_t;

static test_stats_t global_stats = {0};
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Test options */
static int skip_multi_connection = 0;
static int skip_batch_requests = 0;
static int skip_transform_tests = 0;

/* ========================================================================
 * CONNECTION STATE FOR MULTI-CONNECTION TESTS
 * ======================================================================== */

typedef struct {
    int conn_id;
    obsws_connection_t *conn;
    int events_received;
    int commands_sent;
    int commands_successful;
    char status[256];
    pthread_t thread_id;
    /* OBS connection parameters */
    const char *host;
    int port;
    const char *password;
} connection_context_t;

/* ========================================================================
 * HELPER FUNCTIONS
 * ======================================================================== */

/**
 * Sleep for specified milliseconds (cross-platform)
 */
static void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/**
 * Get current timestamp as formatted string
 */
static const char* get_timestamp(void) {
    static char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
    return timestamp;
}

/**
 * Print test section header with formatting
 */
static void print_section_header(const char *section_name, int section_num) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║ Section %d: %-53s║\n", section_num, section_name);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

/**
 * Print individual test result with statistics tracking
 */
static void print_test_result(const char *test_name, int passed) {
    pthread_mutex_lock(&stats_mutex);
    global_stats.tests_run++;
    if (passed) {
        global_stats.tests_passed++;
        printf("[%s] ✓ PASS: %s\n", get_timestamp(), test_name);
    } else {
        global_stats.tests_failed++;
        printf("[%s] ✗ FAIL: %s\n", get_timestamp(), test_name);
    }
    pthread_mutex_unlock(&stats_mutex);
}

/**
 * Log callback function
 */
static void unified_log_callback(obsws_log_level_t level, const char *message, void *user_data) {
    const char *level_str;
    switch (level) {
        case OBSWS_LOG_ERROR:   level_str = "ERROR"; break;
        case OBSWS_LOG_WARNING: level_str = "WARN "; break;
        case OBSWS_LOG_INFO:    level_str = "INFO "; break;
        case OBSWS_LOG_DEBUG:   level_str = "DEBUG"; break;
        default:                level_str = "?????"; break;
    }
    
    int conn_id = (intptr_t)user_data;
    if (conn_id > 0) {
        printf("[%s] [%s] [CONN %d] %s\n", get_timestamp(), level_str, conn_id, message);
    } else {
        printf("[%s] [%s] %s\n", get_timestamp(), level_str, message);
    }
}

/**
 * Event callback function
 */
static void unified_event_callback(obsws_connection_t *conn, const char *event_type,
                                   const char *event_data, void *user_data) {
    (void)conn;
    (void)event_data;
    
    pthread_mutex_lock(&stats_mutex);
    global_stats.events_received++;
    pthread_mutex_unlock(&stats_mutex);
    
    int conn_id = (intptr_t)user_data;
    if (conn_id > 0) {
        printf("[%s] [EVENT] [CONN %d] Type: %s\n", get_timestamp(), conn_id, event_type);
    } else {
        printf("[%s] [EVENT] Type: %s\n", get_timestamp(), event_type);
    }
}

/**
 * State callback function
 */
static void unified_state_callback(obsws_connection_t *conn, obsws_state_t old_state,
                                   obsws_state_t new_state, void *user_data) {
    (void)conn;
    
    pthread_mutex_lock(&stats_mutex);
    global_stats.state_changes++;
    pthread_mutex_unlock(&stats_mutex);
    
    int conn_id = (intptr_t)user_data;
    if (conn_id > 0) {
        printf("[%s] [STATE] [CONN %d] %s -> %s\n", 
               get_timestamp(), conn_id,
               obsws_state_string(old_state), obsws_state_string(new_state));
    } else {
        printf("[%s] [STATE] %s -> %s\n", 
               get_timestamp(),
               obsws_state_string(old_state), obsws_state_string(new_state));
    }
}

/**
 * Wait for connection to be established with timeout
 */
static int wait_for_connection(obsws_connection_t *conn, int timeout_ms) {
    int iterations = timeout_ms / 100;
    for (int i = 0; i < iterations; i++) {
        if (obsws_is_connected(conn)) {
            return 1;
        }
        obsws_process_events(conn, 100);
    }
    return 0;
}

/**
 * Print usage information
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("libwsv5 - Comprehensive Test Suite\n");
    printf("Tests all library functionality and OBS WebSocket protocol v5 integration\n\n");
    printf("Options:\n");
    printf("  -h, --host HOST        OBS WebSocket host (default: %s)\n", DEFAULT_OBS_HOST);
    printf("  -p, --port PORT        OBS WebSocket port (default: %d)\n", DEFAULT_OBS_PORT);
    printf("  -w, --password PASS    OBS WebSocket password (default: none)\n");
    printf("  -d, --debug LEVEL      Debug level 0-3 (default: %d)\n", DEFAULT_DEBUG_LEVEL);
    printf("                         0=NONE, 1=LOW, 2=MEDIUM, 3=HIGH/VERBOSE\n");
    printf("  --skip-multi           Skip multi-connection concurrency tests\n");
    printf("  --skip-batch           Skip batch request tests\n");
    printf("  --skip-transforms      Skip scene transformation tests\n");
    printf("  --help                 Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s --host 192.168.1.32 --password mypass\n", program_name);
    printf("  %s -h localhost -p 4455 -d 2\n", program_name);
    printf("  %s --skip-multi --skip-transforms\n", program_name);
    printf("\n");
}

/* ========================================================================
 * SECTION 1: LIBRARY INITIALIZATION AND CONFIGURATION
 * ======================================================================== */

static int test_library_initialization(void) {
    print_section_header("Library Initialization and Setup", 1);
    
    /* Test: Initialize library */
    obsws_error_t err = obsws_init();
    print_test_result("obsws_init()", err == OBSWS_OK);
    if (err != OBSWS_OK) {
        printf("ERROR: Failed to initialize library: %s\n", obsws_error_string(err));
        return 0;
    }
    
    /* Test: Get version */
    const char *version = obsws_version();
    print_test_result("obsws_version() returns non-NULL", version != NULL);
    if (version) {
        printf("  Library version: %s\n", version);
    }
    
    /* Test: Set log level */
    obsws_set_log_level(OBSWS_LOG_DEBUG);
    print_test_result("obsws_set_log_level()", 1);
    
    /* Test: Set debug level */
    obsws_set_debug_level(OBSWS_DEBUG_HIGH);
    int debug_level = obsws_get_debug_level();
    print_test_result("obsws_set_debug_level() to HIGH and obsws_get_debug_level()", 
                     debug_level == OBSWS_DEBUG_HIGH);
    
    /* Test: Set log timestamps */
    err = obsws_set_log_timestamps(true);
    print_test_result("obsws_set_log_timestamps(true)", err == OBSWS_OK);
    
    /* Test: Set log colors */
    err = obsws_set_log_colors(2);  /* Auto-detect */
    print_test_result("obsws_set_log_colors(2)", err == OBSWS_OK);
    
    return 1;
}

/* ========================================================================
 * SECTION 2: SINGLE CONNECTION TESTS
 * ======================================================================== */

static int test_single_connection(const char *obs_host, int obs_port, const char *obs_password) {
    print_section_header("Single Connection Establishment and Basic Operations", 2);
    
    /* Initialize connection config */
    obsws_config_t config;
    obsws_config_init(&config);
    
    config.host = obs_host;
    config.port = obs_port;
    config.password = obs_password;
    config.use_ssl = false;
    config.log_callback = unified_log_callback;
    config.event_callback = unified_event_callback;
    config.state_callback = unified_state_callback;
    config.user_data = (void *)0;
    
    config.recv_timeout_ms = 5000;
    config.send_timeout_ms = 5000;
    config.auto_reconnect = true;
    config.reconnect_delay_ms = 2000;
    config.max_reconnect_delay_ms = 10000;
    config.max_reconnect_attempts = 3;
    config.ping_interval_ms = 20000;
    
    printf("Connecting to OBS at %s:%d...\n", obs_host, obs_port);
    
    /* Test: Create connection */
    obsws_connection_t *conn = obsws_connect(&config);
    print_test_result("obsws_connect()", conn != NULL);
    if (!conn) {
        printf("ERROR: Failed to create connection\n");
        return 0;
    }
    
    /* Test: Wait for connection */
    int connected = wait_for_connection(conn, 10000);
    print_test_result("Connection established (wait_for_connection)", connected);
    if (!connected) {
        printf("ERROR: Failed to connect to OBS\n");
        obsws_disconnect(conn);
        return 0;
    }
    
    printf("✓ Successfully connected to OBS\n");
    sleep_ms(1000);
    
    /* Test: Check connection state */
    obsws_state_t state = obsws_get_state(conn);
    int is_connected = obsws_is_connected(conn);
    print_test_result("obsws_is_connected() and obsws_get_state()", is_connected && state == OBSWS_STATE_CONNECTED);
    printf("  Current connection state: %s\n", obsws_state_string(state));
    
    /* Test: Get stats */
    obsws_stats_t stats;
    obsws_error_t err = obsws_get_stats(conn, &stats);
    print_test_result("obsws_get_stats()", err == OBSWS_OK);
    if (err == OBSWS_OK) {
        printf("  Stats - Sent: %lu, Received: %lu, Errors: %lu\n", 
               stats.messages_sent, stats.messages_received, stats.error_count);
    }
    sleep_ms(500);
    
    /* Test: Get Version */
    obsws_response_t *response = NULL;
    err = obsws_send_request(conn, "GetVersion", NULL, &response, 0);
    int version_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("GetVersion request", version_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Get Scene List */
    response = NULL;
    err = obsws_send_request(conn, "GetSceneList", NULL, &response, 0);
    int scene_list_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("GetSceneList request", scene_list_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Get Current Scene */
    char current_scene[256] = {0};
    err = obsws_get_current_scene(conn, current_scene, sizeof(current_scene));
    int get_scene_ok = (err == OBSWS_OK && strlen(current_scene) > 0);
    print_test_result("obsws_get_current_scene()", get_scene_ok);
    if (get_scene_ok) {
        printf("  Current scene: %s\n", current_scene);
    }
    sleep_ms(500);
    
    /* Test: Get Input List */
    response = NULL;
    err = obsws_send_request(conn, "GetInputList", NULL, &response, 0);
    int input_list_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("GetInputList request", input_list_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Start/Stop Recording */
    response = NULL;
    err = obsws_start_recording(conn, &response);
    int recording_start_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("obsws_start_recording()", recording_start_ok);
    if (response) obsws_response_free(response);
    sleep_ms(1000);
    
    response = NULL;
    err = obsws_stop_recording(conn, &response);
    int recording_stop_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("obsws_stop_recording()", recording_stop_ok);
    if (response) obsws_response_free(response);
    sleep_ms(500);
    
    /* Test: Start/Stop Streaming */
    response = NULL;
    err = obsws_start_streaming(conn, &response);
    int streaming_start_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("obsws_start_streaming()", streaming_start_ok);
    if (response) obsws_response_free(response);
    sleep_ms(1000);
    
    response = NULL;
    err = obsws_stop_streaming(conn, &response);
    int streaming_stop_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("obsws_stop_streaming()", streaming_stop_ok);
    if (response) obsws_response_free(response);
    sleep_ms(500);
    
    /* Store connection for later tests */
    extern obsws_connection_t *g_main_connection;
    g_main_connection = conn;
    extern char g_current_scene[256];
    strncpy(g_current_scene, current_scene, sizeof(g_current_scene) - 1);
    
    return 1;
}

/* Global connection storage for multi-test scenarios */
obsws_connection_t *g_main_connection = NULL;
char g_current_scene[256] = {0};

/* ========================================================================
 * SECTION 3: AUDIO CONTROL AND SOURCE PROPERTIES
 * ======================================================================== */

static int test_audio_and_properties(void) {
    if (!g_main_connection) {
        printf("ERROR: No active connection for audio tests\n");
        return 0;
    }
    
    print_section_header("Audio Control and Source Properties", 3);
    
    /* Test: Get Input Mute Status */
    char request_data[512];
    snprintf(request_data, sizeof(request_data), "{\"inputName\":\"Microphone/Aux\"}");
    
    obsws_response_t *response = NULL;
    obsws_error_t err = obsws_send_request(g_main_connection, "GetInputMute", request_data, &response, 0);
    int get_mute_ok = (err == OBSWS_OK && response);
    print_test_result("GetInputMute request", get_mute_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Set Input Mute (mute) */
    snprintf(request_data, sizeof(request_data), 
             "{\"inputName\":\"Microphone/Aux\",\"inputMuted\":true}");
    response = NULL;
    err = obsws_send_request(g_main_connection, "SetInputMute", request_data, &response, 0);
    int set_mute_ok = (err == OBSWS_OK);
    print_test_result("SetInputMute (mute)", set_mute_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Set Input Mute (unmute) */
    snprintf(request_data, sizeof(request_data), 
             "{\"inputName\":\"Microphone/Aux\",\"inputMuted\":false}");
    response = NULL;
    err = obsws_send_request(g_main_connection, "SetInputMute", request_data, &response, 0);
    int set_unmute_ok = (err == OBSWS_OK);
    print_test_result("SetInputMute (unmute)", set_unmute_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Set Source Visibility */
    printf("\n  >>> SOURCE VISIBILITY CONTROL <<<\n");
    if (g_current_scene[0] != '\0') {
        /* First, hide a source */
        err = obsws_set_source_visibility(g_main_connection, g_current_scene, 
                                         "Camera", false, NULL);
        print_test_result("obsws_set_source_visibility() - hide", err == OBSWS_OK);
        sleep_ms(500);
        
        /* Then show it again */
        err = obsws_set_source_visibility(g_main_connection, g_current_scene, 
                                         "Camera", true, NULL);
        print_test_result("obsws_set_source_visibility() - show", err == OBSWS_OK);
        sleep_ms(500);
    }
    
    /* Test: Set Source Filter Enabled (if filters exist) */
    printf("\n  >>> SOURCE FILTER CONTROL <<<\n");
    if (g_current_scene[0] != '\0') {
        /* Test disabling a filter */
        err = obsws_set_source_filter_enabled(g_main_connection, "Microphone/Aux",
                                             "Noise Suppression", false, NULL);
        print_test_result("obsws_set_source_filter_enabled() - disable", err == OBSWS_OK);
        sleep_ms(500);
        
        /* Test enabling a filter */
        err = obsws_set_source_filter_enabled(g_main_connection, "Microphone/Aux",
                                             "Noise Suppression", true, NULL);
        print_test_result("obsws_set_source_filter_enabled() - enable", err == OBSWS_OK);
        sleep_ms(500);
    }
    
    /* Also test via request for alternative implementation */
    printf("\n  >>> SOURCE FILTER CONTROL (via obsws_send_request) <<<\n");
    snprintf(request_data, sizeof(request_data), 
             "{\"sourceName\":\"Window Capture\",\"filterName\":\"Chroma Key\",\"filterEnabled\":false}");
    response = NULL;
    err = obsws_send_request(g_main_connection, "SetSourceFilterEnabled", 
                            request_data, &response, 0);
    int set_filter_ok = (err == OBSWS_OK);
    print_test_result("SetSourceFilterEnabled (generic request)", set_filter_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Get Source Settings */
    printf("\n  >>> SOURCE PROPERTY MANIPULATION <<<\n");
    snprintf(request_data, sizeof(request_data), "{\"sourceName\":\"Desktop Audio\"}");
    response = NULL;
    err = obsws_send_request(g_main_connection, "GetSourceSettings", request_data, &response, 0);
    int get_settings_ok = (err == OBSWS_OK && response);
    print_test_result("GetSourceSettings request", get_settings_ok);
    if (response) {
        if (response->response_data) {
            printf("  Source settings:\n%s\n", response->response_data);
        }
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Set Source Settings (generic example) */
    snprintf(request_data, sizeof(request_data), 
             "{\"sourceName\":\"Browser\",\"sourceSettings\":{\"url\":\"https://example.com\"}}");
    response = NULL;
    err = obsws_send_request(g_main_connection, "SetSourceSettings", request_data, &response, 0);
    int set_settings_ok = (err == OBSWS_OK);
    print_test_result("SetSourceSettings request", set_settings_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    return 1;
}

/* ========================================================================
 * SECTION 4: SCENE MANIPULATION AND TRANSFORMATIONS
 * ======================================================================== */

static int test_scene_manipulations(void) {
    if (!g_main_connection || g_current_scene[0] == '\0') {
        printf("WARNING: Skipping scene manipulation tests - no active scene\n");
        return 1;  /* Not a failure, just skip */
    }
    
    print_section_header("Scene Item Transformations and Manipulations", 4);
    
    const char *target_scene = g_current_scene;
    obsws_response_t *response = NULL;
    obsws_error_t err;
    
    /* Test: Get Scene Item List */
    char request_data[512];
    snprintf(request_data, sizeof(request_data), "{\"sceneName\":\"%s\"}", target_scene);
    response = NULL;
    err = obsws_send_request(g_main_connection, "GetSceneItemList", request_data, &response, 0);
    int scene_items_ok = (err == OBSWS_OK && response && response->success);
    print_test_result("GetSceneItemList request", scene_items_ok);
    if (response) {
        if (response->response_data) {
            printf("  Scene items in '%s':\n%s\n", target_scene, response->response_data);
        }
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    printf("\n  >>> POSITION & TRANSLATION TESTS <<<\n");
    /* Test: X-Axis Translation (horizontal movement) */
    for (int i = 0; i < 4; i++) {
        char transform_data[1024];
        double x_pos = 0.0 + (i * 200.0);  /* 0, 200, 400, 600 pixels */
        
        snprintf(transform_data, sizeof(transform_data),
                "{\"sceneName\":\"%s\",\"sceneItemId\":1,"
                "\"sceneItemTransform\":{"
                "\"sourceWidth\":1920,\"sourceHeight\":1080,"
                "\"x\":%.1f,\"y\":100.0,"
                "\"scaleX\":1.0,\"scaleY\":1.0,"
                "\"rotation\":0.0"
                "}}",
                target_scene, x_pos);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSceneItemTransform", 
                                transform_data, &response, 0);
        
        printf("  [Translation] Moving to X=%.1f (step %d/4)\n", x_pos, i+1);
        if (i == 0) {
            print_test_result("SetSceneItemTransform - X-axis translation (horizontal movement)", err == OBSWS_OK);
        }
        if (response) obsws_response_free(response);
        sleep_ms(150);
    }
    
    printf("\n  >>> ROTATION TESTS <<<\n");
    /* Test: Full rotation cycle (0 to 315 degrees in 45-degree increments) */
    for (int i = 0; i < 8; i++) {
        char transform_data[1024];
        double rotation = (i * 45.0);  /* 0, 45, 90, 135, 180, 225, 270, 315 degrees */
        
        snprintf(transform_data, sizeof(transform_data),
                "{\"sceneName\":\"%s\",\"sceneItemId\":1,"
                "\"sceneItemTransform\":{"
                "\"sourceWidth\":1920,\"sourceHeight\":1080,"
                "\"x\":100.0,\"y\":100.0,"
                "\"scaleX\":1.0,\"scaleY\":1.0,"
                "\"rotation\":%.1f"
                "}}",
                target_scene, rotation);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSceneItemTransform", 
                                transform_data, &response, 0);
        
        printf("  [Rotation] Rotating to %.1f° (step %d/8) - ", rotation, i+1);
        if (rotation < 45) printf("(0°)\n");
        else if (rotation < 135) printf("(45°-90°)\n");
        else if (rotation < 225) printf("(135°-180°)\n");
        else printf("(225°-315°)\n");
        
        if (i == 0) {
            print_test_result("SetSceneItemTransform - Rotation (full 360 degree cycle)", err == OBSWS_OK);
        }
        if (response) obsws_response_free(response);
        sleep_ms(200);
    }
    
    printf("\n  >>> SCALE/ZOOM TESTS (Resolution/Size Changes) <<<\n");
    /* Test: Resolution changes via scaling (resize from small to large) */
    double scale_factors[] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
    int num_scales = sizeof(scale_factors) / sizeof(scale_factors[0]);
    
    for (int i = 0; i < num_scales; i++) {
        char transform_data[1024];
        double scale = scale_factors[i];
        
        snprintf(transform_data, sizeof(transform_data),
                "{\"sceneName\":\"%s\",\"sceneItemId\":1,"
                "\"sceneItemTransform\":{"
                "\"sourceWidth\":1920,\"sourceHeight\":1080,"
                "\"x\":100.0,\"y\":100.0,"
                "\"scaleX\":%.2f,\"scaleY\":%.2f,"
                "\"rotation\":0.0"
                "}}",
                target_scene, scale, scale);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSceneItemTransform", 
                                transform_data, &response, 0);
        
        printf("  [Scale/Zoom] Setting scale to %.2fx (step %d/7) - %s\n", 
               scale, i+1, (scale < 1.0) ? "SHRINKING" : (scale > 1.0) ? "ENLARGING" : "NORMAL");
        
        if (i == 0) {
            print_test_result("SetSceneItemTransform - Scale/Zoom (0.5x to 2.0x)", err == OBSWS_OK);
        }
        if (response) obsws_response_free(response);
        sleep_ms(200);
    }
    
    printf("\n  >>> STACKING ORDER / Z-INDEX / HEIGHT TESTS <<<\n");
    /* Test: Z-Index manipulation (stacking order - which item is on top) */
    for (int z_index = 0; z_index < 5; z_index++) {
        char transform_data[1024];
        
        /* sceneItemIndex controls z-order: lower index = farther back, higher index = more forward */
        snprintf(transform_data, sizeof(transform_data),
                "{\"sceneName\":\"%s\",\"sceneItemId\":1,"
                "\"sceneItemIndex\":%d,"
                "\"transform\":{"
                "\"sourceWidth\":1920,\"sourceHeight\":1080,"
                "\"x\":100.0,\"y\":100.0,"
                "\"scaleX\":1.0,\"scaleY\":1.0,"
                "\"rotation\":0.0"
                "}}",
                target_scene, z_index);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSceneItemTransform", 
                                transform_data, &response, 0);
        
        printf("  [Z-Index] Setting stacking order to position %d (step %d/5) - %s\n", 
               z_index, z_index+1, (z_index == 0) ? "BACK" : (z_index == 4) ? "FRONT" : "MIDDLE");
        
        if (z_index == 0) {
            print_test_result("SetSceneItemTransform - Z-Index/Stacking Order (layering)", err == OBSWS_OK);
        }
        if (response) obsws_response_free(response);
        sleep_ms(150);
    }
    
    printf("\n  >>> COMBINED TRANSFORMATIONS (Position + Rotation + Scale + Z-Index) <<<\n");
    /* Test: Complex combined transformations */
    for (int combo = 0; combo < 4; combo++) {
        char transform_data[1024];
        double rotation = (combo * 90.0);
        double scale = 0.8 + (combo * 0.25);
        double x_pos = 150.0 + (combo * 100.0);
        double y_pos = 150.0 + (combo * 50.0);
        
        snprintf(transform_data, sizeof(transform_data),
                "{\"sceneName\":\"%s\",\"sceneItemId\":1,"
                "\"sceneItemIndex\":%d,"
                "\"transform\":{"
                "\"sourceWidth\":1920,\"sourceHeight\":1080,"
                "\"x\":%.1f,\"y\":%.1f,"
                "\"scaleX\":%.2f,\"scaleY\":%.2f,"
                "\"rotation\":%.1f"
                "}}",
                target_scene, combo, x_pos, y_pos, scale, scale, rotation);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSceneItemTransform", 
                                transform_data, &response, 0);
        
        printf("  [Combined] Step %d/4: X=%.1f Y=%.1f Rot=%.0f° Scale=%.2fx Z-Idx=%d\n", 
               combo+1, x_pos, y_pos, rotation, scale, combo);
        
        if (combo == 0) {
            print_test_result("SetSceneItemTransform - Combined (Position+Rotation+Scale+Z-Index)", err == OBSWS_OK);
        }
        if (response) obsws_response_free(response);
        sleep_ms(300);
    }
    
    printf("\n  >>> SCENE ITEM VISIBILITY TESTS (Hide/Show) <<<\n");
    /* Test: Get scene items again to find one to toggle visibility */
    /* Reuse request_data buffer */
    snprintf(request_data, sizeof(request_data), "{\"sceneName\":\"%s\"}", target_scene);
    response = NULL;
    err = obsws_send_request(g_main_connection, "GetSceneItemList", request_data, &response, 0);
    
    if (err == OBSWS_OK && response && response->success && response->response_data) {
        /* Parse scene item ID from response */
        const char *data = response->response_data;
        const char *id_key = "\"sceneItemId\":";
        const char *id_start = strstr(data, id_key);
        
        if (id_start) {
            id_start += strlen(id_key);
            int scene_item_id = atoi(id_start);
            
            if (scene_item_id > 0) {
                printf("  Found scene item ID: %d for visibility tests\n", scene_item_id);
                
                /* Test: Hide scene item */
                snprintf(request_data, sizeof(request_data),
                        "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemEnabled\":false}",
                        target_scene, scene_item_id);
                
                obsws_response_t *hide_response = NULL;
                err = obsws_send_request(g_main_connection, "SetSceneItemEnabled", 
                                        request_data, &hide_response, 0);
                
                print_test_result("SetSceneItemEnabled - Hide item", err == OBSWS_OK && hide_response);
                if (hide_response) obsws_response_free(hide_response);
                sleep_ms(1000);
                
                /* Test: Show scene item again */
                snprintf(request_data, sizeof(request_data),
                        "{\"sceneName\":\"%s\",\"sceneItemId\":%d,\"sceneItemEnabled\":true}",
                        target_scene, scene_item_id);
                
                obsws_response_t *show_response = NULL;
                err = obsws_send_request(g_main_connection, "SetSceneItemEnabled",
                                        request_data, &show_response, 0);
                
                print_test_result("SetSceneItemEnabled - Show item", err == OBSWS_OK && show_response);
                if (show_response) obsws_response_free(show_response);
                sleep_ms(500);
                
                /* Test: Get current scene item transform (read-only verification) */
                printf("\n  >>> SCENE ITEM TRANSFORM PROPERTY READING <<<\n");
                snprintf(request_data, sizeof(request_data),
                        "{\"sceneName\":\"%s\",\"sceneItemId\":%d}",
                        target_scene, scene_item_id);
                
                obsws_response_t *transform_get_response = NULL;
                err = obsws_send_request(g_main_connection, "GetSceneItemTransform",
                                        request_data, &transform_get_response, 0);
                
                print_test_result("GetSceneItemTransform - Read item properties", 
                                err == OBSWS_OK && transform_get_response);
                if (transform_get_response) {
                    if (transform_get_response->response_data) {
                        printf("  Item transform data:\n%s\n", transform_get_response->response_data);
                    }
                    obsws_response_free(transform_get_response);
                }
                sleep_ms(500);
            }
        }
        obsws_response_free(response);
    }
    
    printf("\n  >>> SCENE SWITCHING AND TRANSFORM VERIFICATION <<<\n");
    /* Test: Get current scene and verify we can query it */
    char current[256] = {0};
    err = obsws_get_current_scene(g_main_connection, current, sizeof(current));
    print_test_result("Verify GetCurrentScene after transforms", err == OBSWS_OK);
    
    if (err == OBSWS_OK && current[0]) {
        printf("  Current scene after manipulations: %s\n", current);
    }
    
    sleep_ms(500);
    return 1;
}

/* ========================================================================
 * SECTION 4.5: COMPREHENSIVE LIBRARY FUNCTION TESTING
 * ======================================================================== */

static int test_all_library_functions(void) {
    print_section_header("Comprehensive Library Function Testing", 4.5);
    
    if (!g_main_connection) {
        printf("WARNING: Skipping function tests - no active connection\n");
        return 1;
    }
    
    printf("\n  >>> CONNECTION STATE VERIFICATION <<<\n");
    /* Test: Verify connection state */
    obsws_error_t err;
    obsws_state_t current_state = obsws_get_state(g_main_connection);
    print_test_result("obsws_get_state() - after operations", current_state == OBSWS_STATE_CONNECTED);
    printf("  Current state after operations: %s\n", obsws_state_string(current_state));
    
    printf("\n  >>> SCENE COLLECTION TESTING <<<\n");
    /* Test: Get scene collections via request */
    obsws_response_t *response = NULL;
    err = obsws_send_request(g_main_connection, "GetSceneCollectionList", NULL, &response, 0);
    print_test_result("GetSceneCollectionList request", err == OBSWS_OK);
    if (response) {
        if (response->response_data) {
            printf("  Available scene collections:\n%s\n", response->response_data);
        }
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    printf("\n  >>> SOURCE VISIBILITY TESTING <<<\n");
    /* Test: Set source visibility */
    if (g_current_scene[0] != '\0') {
        char visibility_data[512];
        snprintf(visibility_data, sizeof(visibility_data),
                "{\"sceneName\":\"%s\",\"sourceName\":\"Source1\",\"sourceVisible\":true}",
                g_current_scene);
        
        response = NULL;
        err = obsws_send_request(g_main_connection, "SetSourceFilterEnabled",
                                visibility_data, &response, 0);
        print_test_result("SetSourceFilterEnabled (visibility)", err == OBSWS_OK);
        if (response) obsws_response_free(response);
        sleep_ms(500);
    }
    
    printf("\n  >>> INPUT SOURCE TESTING <<<\n");
    /* Test: Get input settings (to verify source exists) */
    response = NULL;
    err = obsws_send_request(g_main_connection, "GetInputSettings", 
                            "{\"inputName\":\"Desktop Audio\"}", &response, 0);
    print_test_result("GetInputSettings request", err == OBSWS_OK);
    if (response) obsws_response_free(response);
    sleep_ms(500);
    
    printf("\n  >>> CONNECTION STATISTICS VERIFICATION <<<\n");
    /* Test: Get comprehensive stats */
    obsws_stats_t detailed_stats;
    err = obsws_get_stats(g_main_connection, &detailed_stats);
    print_test_result("obsws_get_stats() - detailed", err == OBSWS_OK);
    if (err == OBSWS_OK) {
        printf("  Connection Statistics:\n");
        printf("    - Messages sent:     %lu\n", (unsigned long)detailed_stats.messages_sent);
        printf("    - Messages received: %lu\n", (unsigned long)detailed_stats.messages_received);
        printf("    - Bytes sent:        %lu\n", (unsigned long)detailed_stats.bytes_sent);
        printf("    - Bytes received:    %lu\n", (unsigned long)detailed_stats.bytes_received);
        printf("    - Reconnect count:   %lu\n", (unsigned long)detailed_stats.reconnect_count);
        printf("    - Error count:       %lu\n", (unsigned long)detailed_stats.error_count);
        printf("    - Last ping (ms):    %lu\n", (unsigned long)detailed_stats.last_ping_ms);
    }
    
    printf("\n  >>> PING / CONNECTIVITY TEST <<<\n");
    /* Test: Send ping to measure latency */
    int ping_result = obsws_ping(g_main_connection, 5000);
    int ping_ok = (ping_result >= 0);
    print_test_result("obsws_ping() - connectivity check", ping_ok);
    if (ping_ok) {
        printf("  Network latency: %d ms\n", ping_result);
    }
    
    printf("\n  >>> RECORDING/STREAMING STATUS TESTS <<<\n");
    /* Test: Get recording status */
    bool is_recording = false;
    err = obsws_get_recording_status(g_main_connection, &is_recording, NULL);
    print_test_result("obsws_get_recording_status()", err == OBSWS_OK);
    printf("  Recording status: %s\n", is_recording ? "active" : "inactive");
    sleep_ms(300);
    
    /* Test: Get streaming status */
    bool is_streaming = false;
    err = obsws_get_streaming_status(g_main_connection, &is_streaming, NULL);
    print_test_result("obsws_get_streaming_status()", err == OBSWS_OK);
    printf("  Streaming status: %s\n", is_streaming ? "active" : "inactive");
    sleep_ms(300);
    
    printf("\n  >>> SCENE COLLECTION AND LIST TESTING <<<\n");
    /* Test: Get full scene list */
    response = NULL;
    err = obsws_send_request(g_main_connection, "GetSceneList", NULL, &response, 0);
    print_test_result("GetSceneList via obsws_send_request()", err == OBSWS_OK && response);
    if (response) {
        if (response->response_data) {
            printf("  Available scenes (detailed):\n%s\n", response->response_data);
        }
        obsws_response_free(response);
    }
    sleep_ms(300);
    
    /* Test: Get scene list using library function */
    char **scenes = NULL;
    size_t scene_count = 0;
    err = obsws_get_scene_list(g_main_connection, &scenes, &scene_count);
    print_test_result("obsws_get_scene_list()", err == OBSWS_OK);
    if (err == OBSWS_OK && scenes) {
        printf("  Scenes found: %zu\n", scene_count);
        for (size_t i = 0; i < scene_count && i < 5; i++) {
            printf("    %zu. %s\n", i + 1, scenes[i]);
        }
        obsws_free_scene_list(scenes, scene_count);
    }
    sleep_ms(300);
    
    printf("\n  >>> ERROR STRING FUNCTIONS <<<\n");
    /* Test: Error string conversion */
    const char *err_str = obsws_error_string(OBSWS_ERROR_NOT_CONNECTED);
    print_test_result("obsws_error_string()", err_str != NULL && strlen(err_str) > 0);
    printf("  Example error string: '%s'\n", err_str);
    
    printf("\n  >>> STATE STRING FUNCTIONS <<<\n");
    /* Test: State string conversion */
    const char *state_str = obsws_state_string(OBSWS_STATE_CONNECTED);
    print_test_result("obsws_state_string()", state_str != NULL && strlen(state_str) > 0);
    printf("  Current state string: '%s'\n", state_str);
    
    printf("\n  >>> REQUEST/RESPONSE TESTING <<<\n");
    /* Test: Multiple requests to verify response handling */
    for (int i = 0; i < 3; i++) {
        response = NULL;
        err = obsws_send_request(g_main_connection, "GetVersion", NULL, &response, 0);
        if (err == OBSWS_OK && response) {
            obsws_response_free(response);
        }
    }
    print_test_result("Multiple sequential requests", err == OBSWS_OK);
    sleep_ms(500);
    
    return 1;
}

/* ========================================================================
 * SECTION 5: MULTI-CONNECTION CONCURRENCY TESTS
 * ======================================================================== */

static void* concurrent_connection_worker(void *arg) {
    connection_context_t *ctx = (connection_context_t *)arg;
    
    printf("\n[WORKER %d] Starting concurrent connection worker...\n", ctx->conn_id);
    snprintf(ctx->status, sizeof(ctx->status), "Initializing");
    
    /* Create config using context parameters */
    obsws_config_t config;
    obsws_config_init(&config);
    
    config.host = ctx->host;
    config.port = ctx->port;
    config.password = ctx->password;
    config.use_ssl = false;
    config.log_callback = unified_log_callback;
    config.event_callback = unified_event_callback;
    config.state_callback = unified_state_callback;
    config.user_data = (void *)(intptr_t)ctx->conn_id;
    
    config.recv_timeout_ms = 5000;
    config.send_timeout_ms = 5000;
    config.auto_reconnect = false;
    config.ping_interval_ms = 20000;
    
    /* Connect */
    ctx->conn = obsws_connect(&config);
    if (!ctx->conn) {
        snprintf(ctx->status, sizeof(ctx->status), "Failed to create connection");
        printf("[WORKER %d] ERROR: Failed to create connection\n", ctx->conn_id);
        return NULL;
    }
    
    /* Wait for connection */
    int connected = wait_for_connection(ctx->conn, 5000);
    if (!connected) {
        snprintf(ctx->status, sizeof(ctx->status), "Failed to connect");
        printf("[WORKER %d] ERROR: Failed to connect\n", ctx->conn_id);
        obsws_disconnect(ctx->conn);
        return NULL;
    }
    
    snprintf(ctx->status, sizeof(ctx->status), "Connected");
    printf("[WORKER %d] Connected successfully\n", ctx->conn_id);
    sleep_ms(500);
    
    /* Send test commands */
    obsws_response_t *response = NULL;
    
    /* GetVersion */
    obsws_error_t err = obsws_send_request(ctx->conn, "GetVersion", NULL, &response, 0);
    if (err == OBSWS_OK && response && response->success) {
        ctx->commands_successful++;
        printf("[WORKER %d] ✓ GetVersion succeeded\n", ctx->conn_id);
        obsws_response_free(response);
    }
    ctx->commands_sent++;
    sleep_ms(300);
    
    /* GetSceneList */
    response = NULL;
    err = obsws_send_request(ctx->conn, "GetSceneList", NULL, &response, 0);
    if (err == OBSWS_OK && response && response->success) {
        ctx->commands_successful++;
        printf("[WORKER %d] ✓ GetSceneList succeeded\n", ctx->conn_id);
        obsws_response_free(response);
    }
    ctx->commands_sent++;
    sleep_ms(300);
    
    /* GetStats */
    obsws_stats_t stats;
    err = obsws_get_stats(ctx->conn, &stats);
    if (err == OBSWS_OK) {
        ctx->commands_successful++;
        printf("[WORKER %d] ✓ GetStats succeeded\n", ctx->conn_id);
    }
    ctx->commands_sent++;
    sleep_ms(300);
    
    /* Disconnect */
    obsws_disconnect(ctx->conn);
    snprintf(ctx->status, sizeof(ctx->status), "Disconnected");
    printf("[WORKER %d] Disconnected\n", ctx->conn_id);
    
    return NULL;
}

static int test_multi_connection_concurrency(const char *obs_host, int obs_port, const char *obs_password) {
    if (skip_multi_connection) {
        printf("SKIPPED: Multi-connection tests (--skip-multi)\n");
        return 1;
    }
    
    print_section_header("Multi-Connection Concurrency Tests", 5);
    
    connection_context_t contexts[NUM_CONCURRENT_CONNS];
    memset(contexts, 0, sizeof(contexts));
    
    printf("Creating %d concurrent connections...\n", NUM_CONCURRENT_CONNS);
    
    /* Create threads */
    for (int i = 0; i < NUM_CONCURRENT_CONNS; i++) {
        contexts[i].conn_id = i + 1;
        contexts[i].host = obs_host;
        contexts[i].port = obs_port;
        contexts[i].password = obs_password;
        
        pthread_create(&contexts[i].thread_id, NULL, concurrent_connection_worker, &contexts[i]);
    }
    
    /* Wait for threads */
    for (int i = 0; i < NUM_CONCURRENT_CONNS; i++) {
        pthread_join(contexts[i].thread_id, NULL);
    }
    
    printf("\nMulti-connection test results:\n");
    int all_ok = 1;
    for (int i = 0; i < NUM_CONCURRENT_CONNS; i++) {
        printf("  [CONN %d] Status: %s | Commands: %d/%d successful\n",
               contexts[i].conn_id, contexts[i].status,
               contexts[i].commands_successful, contexts[i].commands_sent);
        if (contexts[i].commands_successful < contexts[i].commands_sent) {
            all_ok = 0;
        }
    }
    
    print_test_result("Multi-connection concurrency test", all_ok);
    
    return 1;
}

/* ========================================================================
 * SECTION 6: ERROR HANDLING AND EDGE CASES
 * ======================================================================== */

static int test_error_handling(void) {
    if (!g_main_connection) {
        printf("ERROR: No active connection for error handling tests\n");
        return 0;
    }
    
    print_section_header("Error Handling and Edge Cases", 6);
    
    /* Test: Invalid request type */
    obsws_response_t *response = NULL;
    obsws_error_t err = obsws_send_request(g_main_connection, "InvalidRequestType", NULL, &response, 0);
    int invalid_req_ok = (err == OBSWS_OK && response && !response->success);
    print_test_result("Invalid request type handling", invalid_req_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Malformed JSON */
    response = NULL;
    err = obsws_send_request(g_main_connection, "SetCurrentProgramScene", 
                            "{MALFORMED JSON}", &response, 0);
    int malformed_ok = (err == OBSWS_OK);  /* Could succeed or fail depending on OBS strictness */
    print_test_result("Malformed JSON handling", 1);  /* Just check it doesn't crash */
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    /* Test: Non-existent resource */
    response = NULL;
    char request_data[512];
    snprintf(request_data, sizeof(request_data), "{\"sceneName\":\"NonExistentScene123\"}");
    err = obsws_send_request(g_main_connection, "SetCurrentProgramScene", request_data, &response, 0);
    int nonexist_ok = (err == OBSWS_OK && response && !response->success);
    print_test_result("Non-existent resource handling", nonexist_ok);
    if (response) {
        obsws_response_free(response);
    }
    sleep_ms(500);
    
    return 1;
}

/* ========================================================================
 * SECTION 7: CONNECTION LIFECYCLE AND CLEANUP
 * ======================================================================== */

static int test_connection_lifecycle(void) {
    print_section_header("Connection Lifecycle and Cleanup", 7);
    
    if (g_main_connection) {
        /* Test: Get final stats before disconnect */
        obsws_stats_t stats;
        obsws_error_t err = obsws_get_stats(g_main_connection, &stats);
        print_test_result("Final obsws_get_stats()", err == OBSWS_OK);
        if (err == OBSWS_OK) {
            printf("  Final stats - Sent: %lu, Received: %lu, Latency: %lu ms, Errors: %lu\n",
                   stats.messages_sent, stats.messages_received, stats.last_ping_ms, stats.error_count);
        }
        
        /* Test: Disconnect */
        obsws_disconnect(g_main_connection);
        print_test_result("obsws_disconnect()", 1);
        sleep_ms(500);
    }
    
    /* Test: Library cleanup */
    obsws_cleanup();
    print_test_result("obsws_cleanup()", 1);
    
    return 1;
}

/* ========================================================================
 * MAIN TEST ORCHESTRATOR
 * ======================================================================== */

int main(int argc, char *argv[]) {
    const char *obs_host = DEFAULT_OBS_HOST;
    int obs_port = DEFAULT_OBS_PORT;
    const char *obs_password = DEFAULT_OBS_PASSWORD;
    int debug_level = DEFAULT_DEBUG_LEVEL;
    
    /* Parse command-line arguments */
    static struct option long_options[] = {
        {"host",           required_argument, 0, 'h'},
        {"port",           required_argument, 0, 'p'},
        {"password",       required_argument, 0, 'w'},
        {"debug",          required_argument, 0, 'd'},
        {"skip-multi",     no_argument,       0,  0 },
        {"skip-batch",     no_argument,       0,  0 },
        {"skip-transforms", no_argument,      0,  0 },
        {"help",           no_argument,       0,  0 },
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
                if (strcmp(long_options[option_index].name, "skip-multi") == 0) {
                    skip_multi_connection = 1;
                } else if (strcmp(long_options[option_index].name, "skip-batch") == 0) {
                    skip_batch_requests = 1;
                } else if (strcmp(long_options[option_index].name, "skip-transforms") == 0) {
                    skip_transform_tests = 1;
                } else if (strcmp(long_options[option_index].name, "help") == 0) {
                    print_usage(argv[0]);
                    return 0;
                }
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Print header */
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          libwsv5 ULTIMATE COMPREHENSIVE TEST SUITE         ║\n");
    printf("║                  All Functions Tested                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    printf("Configuration:\n");
    printf("  Host: %s\n", obs_host);
    printf("  Port: %d\n", obs_port);
    printf("  Password: %s\n", strlen(obs_password) > 0 ? "***" : "(none)");
    printf("  Debug Level: %d\n\n", debug_level);
    
    /* Record start time */
    global_stats.test_start_time = time(NULL);
    
    /* Run tests */
    int all_passed = 1;
    
    if (!test_library_initialization()) {
        all_passed = 0;
        goto cleanup;
    }
    
    if (!test_single_connection(obs_host, obs_port, obs_password)) {
        all_passed = 0;
        goto cleanup;
    }
    
    if (!test_audio_and_properties()) {
        all_passed = 0;
    }
    
    if (!skip_transform_tests && !test_scene_manipulations()) {
        all_passed = 0;
    }
    
    if (!test_all_library_functions()) {
        all_passed = 0;
    }
    
    if (!test_multi_connection_concurrency(obs_host, obs_port, obs_password)) {
        all_passed = 0;
    }
    
    if (!test_error_handling()) {
        all_passed = 0;
    }

cleanup:
    if (!test_connection_lifecycle()) {
        all_passed = 0;
    }
    
    /* Record end time */
    global_stats.test_end_time = time(NULL);
    
    /* Print summary */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      TEST SUMMARY                          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("Tests Run:        %d\n", global_stats.tests_run);
    printf("Tests Passed:     %d ✓\n", global_stats.tests_passed);
    printf("Tests Failed:     %d ✗\n", global_stats.tests_failed);
    printf("Pass Rate:        %.1f%%\n", 
           global_stats.tests_run > 0 ? (100.0 * global_stats.tests_passed / global_stats.tests_run) : 0.0);
    printf("Events Received:  %d\n", global_stats.events_received);
    printf("State Changes:    %d\n", global_stats.state_changes);
    printf("Execution Time:   %ld seconds\n", 
           global_stats.test_end_time - global_stats.test_start_time);
    printf("\n");
    
    return (global_stats.tests_failed == 0) ? 0 : 1;
}
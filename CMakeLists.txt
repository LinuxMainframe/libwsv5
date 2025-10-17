cmake_minimum_required(VERSION 3.10)
project(libwsv5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Find libwebsockets
find_path(LIBWEBSOCKETS_INCLUDE_DIR libwebsockets.h)
find_library(LIBWEBSOCKETS_LIBRARY NAMES websockets)

# Find cJSON
find_path(CJSON_INCLUDE_DIR cjson/cJSON.h)
find_library(CJSON_LIBRARY NAMES cjson)

# Check if all dependencies are found
if(NOT LIBWEBSOCKETS_INCLUDE_DIR OR NOT LIBWEBSOCKETS_LIBRARY)
    message(FATAL_ERROR "libwebsockets not found. Please install libwebsockets-dev")
endif()

if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
    message(FATAL_ERROR "cJSON not found. Please install libcjson-dev")
endif()

# Source files
set(SOURCES
    library.c
)

set(HEADERS
    library.h
)

# Create static library
add_library(libwsv5 STATIC ${SOURCES} ${HEADERS})

# Set library output name (removes 'lib' prefix duplication)
set_target_properties(libwsv5 PROPERTIES OUTPUT_NAME wsv5)

# Include directories
target_include_directories(libwsv5 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${LIBWEBSOCKETS_INCLUDE_DIR}
    ${CJSON_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(libwsv5 PUBLIC
    ${OPENSSL_LIBRARIES}
    ${LIBWEBSOCKETS_LIBRARY}
    ${CJSON_LIBRARY}
    Threads::Threads
    m
)

# Compiler flags
target_compile_options(libwsv5 PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# Optional: Build example/test program
option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(obsws_example example.c)
    target_link_libraries(obsws_example libwsv5)
endif()

# Installation
install(TARGETS libwsv5
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${HEADERS}
    DESTINATION include/libwsv5
)

# Doxygen documentation generation
find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    # Configure Doxyfile
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc)
    set(DOXYGEN_HTML_OUTPUT html)
    set(DOXYGEN_LATEX_OUTPUT latex)
    set(DOXYGEN_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/library.h ${CMAKE_CURRENT_SOURCE_DIR}/library.c)
    set(DOXYGEN_EXCLUDE_PATTERNS "*/test/*" "*/.git/*")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX YES)
    set(DOXYGEN_GENERATE_PDF YES)
    set(DOXYGEN_USE_PDFLATEX YES)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_CALL_GRAPH YES)
    set(DOXYGEN_CALLER_GRAPH YES)
    set(DOXYGEN_HAVE_DOT YES)
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    
    # Add doc target
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    message(STATUS "Doxygen: Found - use 'make doc' to generate documentation")
else()
    message(STATUS "Doxygen: Not found - documentation generation disabled")
    message(STATUS "         Install doxygen to enable: sudo apt-get install doxygen graphviz")
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "libwsv5 Configuration Summary:")
message(STATUS "  Version: 1.0.0")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  OpenSSL: ${OPENSSL_VERSION}")
message(STATUS "  libwebsockets: ${LIBWEBSOCKETS_LIBRARY}")
message(STATUS "  cJSON: ${CJSON_LIBRARY}")
message(STATUS "  Threads: ${CMAKE_THREAD_LIBS_INIT}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "")
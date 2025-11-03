cmake_minimum_required(VERSION 3.10)
project(libwsv5
    LANGUAGES C
    VERSION 1.1.0
    DESCRIPTION "OBS WebSocket v5 Protocol C Library"
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
    libwsv5.c
)

set(HEADERS
    libwsv5.h
)

# Create both static and shared libraries
add_library(libwsv5_static STATIC ${SOURCES} ${HEADERS})
add_library(libwsv5_shared SHARED ${SOURCES} ${HEADERS})

# Set library output names (removes 'lib' prefix duplication)
set_target_properties(libwsv5_static PROPERTIES OUTPUT_NAME wsv5)
set_target_properties(libwsv5_shared PROPERTIES OUTPUT_NAME wsv5)

# Create an alias for convenience - libwsv5 defaults to static
add_library(libwsv5 ALIAS libwsv5_static)

# Include directories for both libraries
foreach(target libwsv5_static libwsv5_shared)
    target_include_directories(${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OPENSSL_INCLUDE_DIR}
        ${LIBWEBSOCKETS_INCLUDE_DIR}
        ${CJSON_INCLUDE_DIR}
    )
    
    # Link libraries
    target_link_libraries(${target} PUBLIC
        ${OPENSSL_LIBRARIES}
        ${LIBWEBSOCKETS_LIBRARY}
        ${CJSON_LIBRARY}
        Threads::Threads
        m
    )
    
    # Compiler flags
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endforeach()

# Optional: Build test program
option(BUILD_TESTS "Build test programs" OFF)

# Build test suite if requested
if(BUILD_TESTS)
    # Comprehensive test suite
    add_executable(test tests/test.c)
    target_link_libraries(test libwsv5_static)
    target_include_directories(test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test PRIVATE -lm)
endif()

# Installation
install(TARGETS libwsv5_static libwsv5_shared
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${HEADERS}
    DESTINATION include/libwsv5
)

# Install pkg-config file
configure_file(libwsv5.pc.in libwsv5.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libwsv5.pc
    DESTINATION lib/pkgconfig
)

# Doxygen documentation generation
find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    # Configure Doxyfile via CMake variables
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc)
    set(DOXYGEN_HTML_OUTPUT html)
    set(DOXYGEN_LATEX_OUTPUT latex)
    set(DOXYGEN_PROJECT_NAME "libwsv5")
    set(DOXYGEN_PROJECT_NUMBER "1.1.0")  # Update this when bumping version
    set(DOXYGEN_PROJECT_BRIEF "OBS WebSocket v5 Protocol C Library")
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
    set(DOXYGEN_HTML_TIMESTAMP YES)
    set(DOXYGEN_SEARCHENGINE YES)
    set(DOXYGEN_SERVER_BASED_SEARCH NO)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_DOT_IMAGE_FORMAT svg)
    set(DOXYGEN_INTERACTIVE_SVG YES)
    
    # Add doc target using modern CMake approach
    doxygen_add_docs(doc
        libwsv5.h libwsv5.c
        COMMENT "Generating API documentation with Doxygen (HTML + LaTeX/PDF)"
    )
    
    message(STATUS "Doxygen: Found - use 'make doc' to generate documentation")
else()
    message(STATUS "Doxygen: Not found - documentation generation disabled")
    message(STATUS "         Install doxygen to enable: sudo apt-get install doxygen graphviz")
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "libwsv5 Configuration Summary:")
message(STATUS "  Version: 1.1.0")
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

# CPack Configuration for packaging
set(CPACK_PACKAGE_NAME "libwsv5")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Aidan A. Bradley")
set(CPACK_PACKAGE_CONTACT "libwsv5-dev@example.com")
set(CPACK_PACKAGE_DESCRIPTION "OBS WebSocket v5 Protocol C Library")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance C library for OBS WebSocket v5 protocol")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/linuxmainframe/libwsv5")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Debian package specific settings
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Aidan A. Bradley <libwsv5-dev@example.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libwebsockets (>= 3.0), libcjson (>= 1.7), libssl-dev (>= 1.1)")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libwsv5-dev (= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_NAME}_${PROJECT_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")

# Source archive
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES "/build/;/cmake-build-*/;/.git/;/.gitignore;/.idea/;/.vscode/")

include(CPack)

message(STATUS "Package Generation:")
message(STATUS "  To create .deb packages: cpack -G DEB")
message(STATUS "  To create source archive: cpack --config CPackSourceConfig.cmake")
message(STATUS "")
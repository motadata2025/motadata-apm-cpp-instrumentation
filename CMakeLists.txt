# =============================================================================
#  motadata-otel/CMakeLists.txt
#
#  Builds the Motadata Instrumentation wrapper as a static library.
#
#  What clients get after building:
#    libmotadata.a        ← link this into their app
#    include/motadata.h   ← include this in their code
#
#  To build this library:
#    cd motadata-otel
#    mkdir build && cd build
#    cmake ..
#    make
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(motadata VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
#  Find Dependencies
# =============================================================================
find_package(opentelemetry-cpp CONFIG REQUIRED)

# =============================================================================
#  Build the static library
# =============================================================================
add_library(motadata STATIC
    src/motadata.cpp
)

# =============================================================================
#  Include directories
# =============================================================================
target_include_directories(motadata
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# =============================================================================
#  Link against OTel SDK libraries
# =============================================================================
target_link_libraries(motadata
    PRIVATE
        opentelemetry-cpp::api
        opentelemetry-cpp::sdk
        opentelemetry-cpp::otlp_http_exporter
        opentelemetry-cpp::otlp_recordable
        opentelemetry-cpp::proto
        opentelemetry-cpp::http_client_curl
        opentelemetry-cpp::ostream_span_exporter
        opentelemetry-cpp::resources
        opentelemetry-cpp::common
        opentelemetry-cpp::trace
        curl
        pthread
)

# =============================================================================
#  Installation rules
# =============================================================================
include(GNUInstallDirs)

install(TARGETS motadata
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES include/motadata.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# =============================================================================
#  Configuration Summary
# =============================================================================
message(STATUS "")
message(STATUS "Motadata Instrumentation configuration:")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  C++ Standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

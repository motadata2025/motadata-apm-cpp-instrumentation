# C++ Application Instrumentation Guide

This guide describes how to add tracing to your C++ application using the **motadata** library.

## What you will achieve
After completing all steps in this guide, your C++ application will automatically send execution traces to your observability system. You will be able to see exactly which functions ran, how long each one took, which ones failed and why, and how requests flow through your entire system.

No Motadata Instrumentation knowledge is required. The **motadata** library hides all of that complexity. You only need to learn five simple function calls.

| Item | Detail |
| :--- | :--- |
| **Time required** | Approximately 30–60 minutes. Most time is the one-time SDK compilation (~20 min). |
| **What you install** | The Motadata Instrumentation C++ SDK (build tools only — stays on your machine) |
| **What you change** | Three lines in `main()`, one include per file, spans around key operations |
| **What you do NOT need** | Any knowledge of instrumentation internals, Protobuf, gRPC, or CMake internals |

---

## Before You Begin: Understanding the Key Concepts

*   **CONCEPT — What is a Trace?** A trace is a complete record of one request or operation flowing through your application.
*   **CONCEPT — What is a Span?** A span is one individual unit of work inside a trace. You create a span by calling `startSpan("functionName")` at the beginning and `span->end()` at the end.
*   **CONCEPT — What is the Motadata Instrumentation SDK?** An industry-standard framework for collecting traces. The **motadata** library is a thin wrapper around this SDK.
*   **CONCEPT — What is CMake?** The build system used to compile C++ projects.
*   **CONCEPT — What is a static library (.a file)?** When you compile the library, the output is `libmotadata.a`, which gets embedded into your application.
*   **CONCEPT — Why must the Motadata Instrumentation SDK be built on the client machine?** C++ libraries are machine-specific. Building it on your machine ensures compatibility with your OS and compiler.
*   **CONCEPT — What is the Motadata Instrumentation Collector?** A background process that receives traces from your application (usually on port 4318) and forwards them to your backend.

---

## PHASE 1: Environment Setup
### Install build tools and compile the Motadata Instrumentation C++ SDK

#### 1. Verify Your Machine Has the Required Tools
Run these commands to ensure you have the necessary versions:
- `cmake --version` (3.16+)
- `g++ --version` (GCC 5+ or Clang 6+)
- `protoc --version` (3.12+)
- `curl --version`
- `git --version`
- `pkg-config --version`

**Install missing tools:**
- **Ubuntu/Debian:** `sudo apt-get install -y build-essential cmake git pkg-config libcurl4-openssl-dev libssl-dev libprotobuf-dev protobuf-compiler`
- **RHEL/CentOS:** `sudo yum groupinstall "Development Tools" && sudo yum install -y cmake3 git pkg-config libcurl-devel openssl-devel protobuf-devel protobuf-compiler`
- **macOS:** `brew install cmake curl openssl protobuf pkg-config`

#### 2. Download the Motadata Instrumentation C++ SDK Source Code
```bash
cd /opt
sudo git clone --depth 1 --branch v1.23.0 https://github.com/open-telemetry/opentelemetry-cpp.git motadata-instrumentation-sdk
cd motadata-instrumentation-sdk
sudo git submodule update --init --recursive
```

#### 3. Compile and Install the Motadata Instrumentation C++ SDK (Takes 15-30 min)
```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=14 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DWITH_OTLP_HTTP=ON \
    -DWITH_OTLP_GRPC=OFF \
    -DBUILD_TESTING=OFF \
    -DWITH_EXAMPLES=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build . -j$(nproc)
sudo cmake --install .
```

---

## PHASE 2: Project Integration
### Add the motadata library to your C++ project

#### 4. Add the Library to Your Project
Instead of copying files manually, you can clone the instrumentation library directly into your project structure.

**Option A: Git Submodule (Recommended)**
```bash
git submodule add https://github.com/motadata2025/motadata-apm-cpp-instrumentation motadata
```

Your project structure should look like this:
```text
your-project/
├── CMakeLists.txt
├── src/
│   └── main.cpp
└── motadata/                   ← The cloned instrumentation library
    ├── include/
    │   └── motadata.h
    ├── src/
    │   └── motadata.cpp
    └── CMakeLists.txt
```

#### 5. Edit Your CMakeLists.txt
Add these two sections to your `CMakeLists.txt` to include and link the library:
```cmake
# 1. Add the subdirectory
add_subdirectory(motadata)

# 2. Link against the library and its dependencies
target_link_libraries(your_app PRIVATE
    motadata
    curl
    pthread
)
```

---

## PHASE 3: Code Instrumentation
### Add spans to your application code

#### 6. Add the Include
Include the header in every file that creates spans:
```cpp
#include "motadata.h"
```

#### 7. Initialize Telemetry in main()
```cpp
int main() {
    motadata::TelemetryConfig cfg;
    cfg.service_name    = "your-service-name";
    cfg.collector_url   = "http://localhost:4318";
    cfg.debug_print_to_console = true; // Set to false in production

    if (!motadata::initTelemetry(cfg)) {
        std::cerr << "[telemetry] Warning: init failed\n";
    }

    // ... your code ...

    motadata::shutdown(); // Flush pending spans before exit
    return 0;
}
```

---

## PHASE 4: Build and Verify

#### 8. Build Your Application
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

#### 9. Run and Verify
Start your application. If `debug_print_to_console` is true, you will see spans in the terminal.
Verify connectivity to the collector:
```bash
curl -X POST http://localhost:4318/v1/traces -H "Content-Type: application/json" -d '{"resourceSpans":[]}'
```

---

## Quick Reference API

| Function | Purpose |
| :--- | :--- |
| `motadata::initTelemetry(cfg)` | Initialize the pipeline (call once in main). |
| `motadata::shutdown()` | Flush and shutdown (call at end of main). |
| `motadata::startSpan("name")` | Start a new span. |
| `span->setAttr("key", value)` | Attach metadata. |
| `span->addEvent("name")` | Add a timestamped breadcrumb. |
| `span->recordException(e)` | Record exception info (use in catch blocks). |
| `span->setOk()` | Mark as successful (green in UI). |
| `span->end()` | **MUST** be called to complete the span. |

---

## Troubleshooting

- **Linker error (curl/pthread):** Ensure `curl` and `pthread` are in `target_link_libraries`.
- **`instrumentation-sdk not found`:** Ensure the SDK was installed to `/usr/local`.
- **`Span destroyed without calling end()`:** You missed a `span->end()` call on some code path.
- **High memory usage:** Reduce `max_queue_size` in `TelemetryConfig`.

# Motadata C++ Instrumentation

![Motadata Premium](https://img.shields.io/badge/Motadata-Instrumentation-blue?style=for-the-badge)
![Version](https://img.shields.io/badge/Version-1.0.0-green?style=for-the-badge)
![C++](https://img.shields.io/badge/C++-14-orange?style=for-the-badge)

A powerful, high-performance C++ wrapper around Motadata Instrumentation for seamless application monitoring and tracing. This package simplifies the integration of observability into C++ applications, providing a clean API to capture spans, attributes, and more.

## High-Level Features

- **Simplified API**: Avoid the boilerplate of raw instrumentation.
- **Premium Performance**: Minimal overhead instrumentation designed for scale.
- **Easy Integration**: Build as a static library and link into any C++ project.
- **Standard Compliant**: Supports OTLP/HTTP export and standard instrumentation resources.

## Prerequisites

Before building, ensure you have the following dependencies installed:

- **CMake** (v3.16 or higher)
- **Compiler** (Supporting C++14)
- **Instrumentation C++ SDK** (Configured with OTLP HTTP support)
- **libcurl**
- **pthread**

## Quick Start

### Building the Library

```bash
mkdir build && cd build
cmake ..
make
```

This will generate `libmotadata.a` in your build directory.

### Integration via Git

The most efficient way to integrate this library is as a Git submodule:

```bash
git submodule add https://github.com/motadata2025/motadata-apm-cpp-instrumentation motadata
```

### Linking to Your Project

1. Add `add_subdirectory(motadata)` to your `CMakeLists.txt`.
2. Link your target against `motadata`, `curl`, and `pthread`.

### Basic Usage

The Motadata Instrumentation library provides a clean C++ interface. Below is a standard usage pattern:

```cpp
#include "motadata.h"

int main() {
    // 1. Initialize telemetry for your service
    motadata::initTelemetry("payment-service", "1.0.0");

    // 2. Start a span to track an operation
    auto span = motadata::startSpan("processPayment");
    
    // 3. Add context and metadata
    span->setAttr("order.id", "ORD-001");
    span->setAttr("amount", 250.50);
    
    // Simulate work
    span->addEvent("Validating payment gateway");

    // 4. End the span when done
    span->end();

    // 5. Clean shutdown before exit
    motadata::shutdown();
    
    return 0;
}
```

## Directory Structure

- `include/`: Public headers (`motadata.h`)
- `src/`: Implementation files
- `build/`: Build artifacts (after running cmake)

## Documentation

For detailed API documentation, please refer to the header file `include/motadata.h`.

## License

Distributed under the [LICENSE](LICENSE) provided in this repository.

---

Designed with ❤️ for high-performance observability.

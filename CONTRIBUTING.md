# Contributing to Motadata C++ Instrumentation

Thank you for your interest in contributing! We welcome contributions to improve the instrumentation wrapper.

## Code of Conduct

Please be respectful and professional in all interactions.

## How to Contribute

1. **Bug Reports**: If you find a bug, please create an issue with detailed steps to reproduce.
2. **Feature Requests**: Suggestions for new features are always welcome!
3. **Pull Requests**:
    - Fork the repository.
    - Create a new branch for your feature or fix.
    - Ensure your code follows the existing style.
    - Submit a pull request with a clear description of the changes.

## Development Setup

To build the project for development:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Coding Standards

- Follow C++14 standards.
- Use meaningful variable and function names.
- Keep the public API in `motadata.h` clean and well-documented.

## Merging

All pull requests will be reviewed before merging. Ensure all tests (if any) pass and the documentation is updated.

DatabaseApp
===========

DatabaseApp is a simple C++17 project demonstrating database connectivity and unit testing.
It uses CMake for building, Conan for dependency management, GoogleTest for unit tests, and SQLite as a lightweight database.

Features
--------

- Modern C++17 codebase
- Unit testing with GoogleTest
- SQLite database integration
- Cross-platform build with CMake
- Docker-based development environment
- VSCode DevContainer support with clangd



- src/        → Application source code
- test/       → Unit tests using GoogleTest
- conanfile.py → Dependency management
- CMakeLists.txt → Build configuration

Prerequisites
-------------

- CMake ≥ 3.15
- Conan ≥ 2.0
- Git
- Docker (optional for development container)

Build Instructions
------------------

1. Using Conan and CMake
    # Create directory and change dir
    mkdir -p build && cd build
    # Install dependencies
    conan install .. --build=missing

    # Build project
    cmake --build build

    # Run the application
    ./build/DatabaseApp/Release/bin/DatabaseApp

2. Run Unit Tests

    ctest --test-dir build --output-on-failure

VSCode DevContainer
------------------

Open the project in VSCode:
- Make sure .devcontainer/devcontainer.json exists
- Click "Reopen in Container"
- All dependencies and extensions are preinstalled, including clangd
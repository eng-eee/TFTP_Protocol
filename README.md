# TFTP Protocol Server

This project implements a simple TFTP-style UDP server in C++.

## Features

- Handles TFTP read and write requests
- Uses UDP sockets for data transfer
- Supports basic error and acknowledgment handling
- Uses log4cxx for logging
- Built with CMake

## Requirements

- CMake 3.20+
- C++17 compiler
- pthreads
- liblog4cxx

## Build

```bash
cmake -S . -B build
cmake --build build -j2
```

## Run

```bash
./build/TFTP_Protocol
```

The server listens on UDP port 69 by default.

## Notes

- The program uses SIGINT, SIGTERM, and SIGQUIT to stop the server gracefully.
- You may need to run it with appropriate permissions if port 69 is restricted.

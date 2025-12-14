# HTTP/1.1 Server in C

A lightweight, educational HTTP/1.1 server implementation in C that demonstrates core web server concepts including concurrent request handling, HTTP protocol parsing, and persistent connections.

## Features

### Core Functionality
- **HTTP/1.0 and HTTP/1.1 Protocol Support** - Handles both protocol versions with appropriate defaults
- **Persistent Connections (Keep-Alive)** - Reduces latency by reusing TCP connections
- **GET Method** - Serves static files with automatic MIME type detection
- **POST Method** - Accepts and processes POST request bodies
- **Path Canonicalization** - Prevents directory traversal attacks using `realpath()`
- **Process-Based Concurrency** - Fork-based model for handling multiple simultaneous connections

### Advanced Features
- **Semaphore-Based Connection Limiting** - Caps concurrent processes at 10 to prevent resource exhaustion
- **Automatic SIGCHLD Handling** - Prevents zombie processes through proper signal handling
- **Query String Parsing** - Extracts URL parameters from requests
- **Content-Type Detection** - Automatically sets correct MIME types for common file extensions
- **Client IP Logging** - Tracks incoming connection sources

### Supported MIME Types
```
HTML    → text/html
CSS     → text/css
JS      → application/js
JSON    → application/json
PDF     → application/pdf
PNG     → image/png
JPG/JPEG → image/jpeg
Others  → application/octet-stream (binary default)
```

**Process Flow:**
1. Parent process listens on port 4040
2. On new connection: `sem_wait()` checks available slots
3. `fork()` creates child process for the client
4. Child handles request(s) in keep-alive loop
5. Child exits, `sem_post()` releases slot
6. SIGCHLD handler reaps zombie processes

### Dependencies
All required headers are standard POSIX:
```c
stdio.h       // Standard I/O
string.h      // String manipulation
stdlib.h      // Memory allocation
sys/socket.h  // Socket operations
signal.h      // Signal handling
semaphore.h   // Process synchronization
limits.h      // PATH_MAX constant
```

## Project Structure

```
http-server/
├── server.c                 # Main server implementation
├── client.c                 # Test client
├── files/                   # Document root (static files)
│   ├── index.html           # Default homepage
│   ├── favicon.ico          # Website icon
│   ├── styles.css           # CSS files
│   ├── form.html            # Simple name and age form to handle POST requests
└── README.md               # This file
```

### Key Code Components

| Component | File Location | Purpose |
|-----------|---------------|---------|
| `HttpRequest` struct | Lines 17-25 | Stores parsed HTTP request data |
| `parse_client_request()` | Lines 30-141 | Parses raw HTTP request into structure |
| `get_header_name()` | Lines 144-178 | Extracts specific header values |
| `handle_connection()` | Lines 181-209 | Determines keep-alive vs close |
| `handle_method()` | Lines 212-403 | Routes and handles GET/POST requests |
| `signal_handler()` | Lines 406-413 | Reaps child processes |
| `main()` | Lines 415-554 | Server initialization and main loop |

### Recommended Usage

**Safe for:**
- Learning HTTP protocol implementation
- Testing in isolated development environments
- Understanding systems programming concepts
- Educational projects and demonstrations

**NOT safe for:**
- Production deployments
- Public-facing servers
- Handling sensitive data
- Internet-exposed services


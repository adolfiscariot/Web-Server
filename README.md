# HTTP/1.1 Server in C

A lightweight, educational HTTP/1.1 server implementation in C that demonstrates core web server concepts including concurrent request handling, HTTP protocol parsing, and persistent connections.

## 📋 Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [HTTP Protocol Support](#http-protocol-support)
- [Configuration](#configuration)
- [Security Considerations](#security-considerations)
- [Limitations](#limitations)
- [Testing](#testing)
- [Troubleshooting](#troubleshooting)
- [Learning Resources](#learning-resources)
- [Contributing](#contributing)
- [License](#license)

## ✨ Features

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

## 🏗️ Architecture

### Concurrency Model
```
┌─────────────────────┐
│   Parent Process    │
│  (Main Loop)        │
│  - accept()         │
│  - fork()           │
└──────────┬──────────┘
           │
           ├──────────────┬──────────────┬──────────────┐
           ▼              ▼              ▼              ▼
    ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
    │ Child 1  │   │ Child 2  │   │ Child 3  │   │   ...    │
    │ Handle   │   │ Handle   │   │ Handle   │   │  (max 10)│
    │ Client A │   │ Client B │   │ Client C │   │          │
    └──────────┘   └──────────┘   └──────────┘   └──────────┘
```

**Process Flow:**
1. Parent process listens on port 4040
2. On new connection: `sem_wait()` checks available slots
3. `fork()` creates child process for the client
4. Child handles request(s) in keep-alive loop
5. Child exits, `sem_post()` releases slot
6. SIGCHLD handler reaps zombie processes

### Request Handling Pipeline
```
Client Request
     ↓
[TCP Accept]
     ↓
[Fork Child Process]
     ↓
[Parse HTTP Request]
     ├─ Method (GET/POST)
     ├─ Path & Query String
     ├─ Protocol Version
     └─ Headers
     ↓
[Validate & Canonicalize Path]
     ↓
[Handle Method]
     ├─ GET  → Read file, send response
     └─ POST → Read body, send acknowledgment
     ↓
[Check Connection Header]
     ├─ Keep-Alive → Loop back to read next request
     └─ Close      → Exit child process
```

## 🔧 Requirements

### System Requirements
- **Operating System:** Linux or Unix-like system (macOS, BSD)
- **Compiler:** GCC or Clang with C99 support
- **Libraries:** POSIX standard libraries (included by default)

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

## 📦 Installation

### 1. Clone or Download
```bash
# Create project directory
mkdir http-server && cd http-server

# Save the server code as server.c
```

### 2. Create Document Root
```bash
# Create directory for serving files
mkdir files

# Add a test HTML file
cat > files/index.html << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>Test Server</title>
</head>
<body>
    <h1>Hello from HTTP Server!</h1>
    <p>This server is working correctly.</p>
</body>
</html>
EOF
```

### 3. Compile
```bash
# Basic compilation
gcc -o server server.c

# With warnings and optimization
gcc -Wall -Wextra -O2 -o server server.c

# With debugging symbols
gcc -g -Wall -o server server.c
```

### 4. Run
```bash
# Start the server
./server

# Output:
# Server listening on port 4040...
```

## 🚀 Usage

### Starting the Server
```bash
./server
```

The server will:
- Bind to `0.0.0.0:4040` (all network interfaces)
- Serve files from the `./files/` directory
- Print client connection information to stdout

### Making Requests

**Using curl:**
```bash
# GET request for homepage
curl http://localhost:4040/

# GET request for specific file
curl http://localhost:4040/index.html

# GET with query parameters
curl http://localhost:4040/search?q=test

# POST request with data
curl -X POST http://localhost:4040/submit \
  -H "Content-Type: application/json" \
  -d '{"name":"John","age":30}'

# Test keep-alive
curl -v --keepalive-time 5 http://localhost:4040/
```

**Using a browser:**
```
http://localhost:4040/
```

**Using Python:**
```python
import requests

# GET request
response = requests.get('http://localhost:4040/')
print(response.text)

# POST request
response = requests.post(
    'http://localhost:4040/form.html',
    json={
	    'user_name': 'Sosa', 
	    'user_age': 300
    }
)
print(response.status_code)
```

### Testing Keep-Alive
```bash
# This script sends multiple requests on same connection
(
  echo -e "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
  sleep 0.5
  echo -e "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
) | nc localhost 4040
```

## 📁 Project Structure

```
http-server/
├── server.c                 # Main server implementation
├── client.c                 # Test client
├── files/                   # Document root (static files)
│   ├── index.html           # Default homepage
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

## 🌐 HTTP Protocol Support

### Request Methods

#### GET
Serves static files from the `files/` directory.

**Example:**
```http
GET /index.html HTTP/1.1
Host: localhost:4040
Connection: keep-alive

→ Returns file content with appropriate Content-Type
```

**Features:**
- Automatic index.html serving for `/` requests
- MIME type detection based on file extension
- 404 responses for missing files
- Binary file support (images, PDFs, etc.)

#### POST
Accepts request body data. Returns 200 OK acknowledgment

**Features:**
- Reads Content-Length header
- Reads exact number of bytes specified
- Validates content length
- Returns success response

### Connection Handling

| Protocol | Default Behavior | Override |
|----------|-----------------|----------|
| HTTP/1.0 | Close after response | `Connection: keep-alive` to persist |
| HTTP/1.1 | Keep connection alive | `Connection: close` to close |

**Keep-Alive Example:**
```http
GET / HTTP/1.1
Connection: keep-alive

← Response headers include: Connection: keep-alive
← Connection remains open for next request
```

### Status Codes

| Code | Description | When Triggered |
|------|-------------|----------------|
| 200 OK | Success | Valid GET/POST request processed |
| 400 Bad Request | Malformed request | Invalid HTTP syntax, missing Content-Length |
| 404 Not Found | Resource doesn't exist | Requested file not in `files/` directory |
| 405 Method Not Allowed | Unsupported method | Methods other than GET/POST |
| 500 Internal Server Error | Server error | Memory allocation failure, file read error |

## ⚙️ Configuration

### Compile-Time Constants

Edit these in `server.c`:

```c
#define OPEN_MAX 10  // Maximum concurrent processes (line 16)

// In main():
addy.sin_port = htons(4040);  // Server port (line 427)

// In handle_method():
const char *directory_name = "files";  // Document root (line 218)
```

## 🔒 Security Considerations

### Implemented Protections

✅ **Path Canonicalization**
```c
realpath(uncanonical_full_path, full_path);
```
Prevents directory traversal attacks like `GET /../../../etc/passwd`

✅ **Connection Limiting**
- Semaphore caps concurrent processes at 10
- Prevents fork bomb attacks

✅ **Header Limits**
- Maximum 20 headers per request
- Prevents header-based DoS attacks

### Known Vulnerabilities ⚠️

This server is **NOT production-ready**. Known issues:

🔴 **No Path Validation After Canonicalization**
- `realpath()` is called but result isn't validated
- Canonicalized path should be checked against document root:
```c
if (strncmp(full_path, canonical_doc_root, strlen(canonical_doc_root)) != 0) {
    // Reject - path escapes document root
}
```

🔴 **Buffer Overflow Risks**
- Fixed 1024-byte buffer for requests
- Large headers/requests will be truncated
- Response header buffer (1024 bytes) can overflow

🔴 **No Request Timeouts**
- Vulnerable to slowloris attacks
- Malicious clients can hold connections indefinitely

🔴 **No Content-Length Validation**
- POST accepts any content length
- Can cause memory exhaustion

🔴 **No Authentication/Authorization**
- Anyone can access any file in `files/`
- No user validation

🔴 **No HTTPS/TLS Support**
- All traffic is plaintext
- Credentials, data visible on network

### Recommended Usage

✅ **Safe for:**
- Learning HTTP protocol implementation
- Testing in isolated development environments
- Understanding systems programming concepts
- Educational projects and demonstrations

❌ **NOT safe for:**
- Production deployments
- Public-facing servers
- Handling sensitive data
- Internet-exposed services

## ⚡ Limitations

### Functional Limitations

| Feature | Supported | Notes |
|---------|-----------|-------|
| HTTP Methods | GET, POST only | No PUT, DELETE, PATCH, OPTIONS |
| HTTP/2 | ❌ | HTTP/1.0 and 1.1 only |
| HTTPS/TLS | ❌ | No encryption support |
| Compression | ❌ | No gzip/deflate encoding |
| Chunked Transfer | ❌ | Requires known Content-Length |
| Range Requests | ❌ | Cannot resume downloads |
| Virtual Hosts | ❌ | Single document root only |
| CGI/FastCGI | ❌ | No dynamic content execution |
| WebSockets | ❌ | No upgrade protocol support |
| Authentication | ❌ | No Basic/Digest/Bearer auth |

### Protocol Compliance Issues

⚠️ **Incomplete Request Reading:**
- Assumes entire request fits in first 1024-byte read
- Large headers or pipelined requests will fail

⚠️ **Missing Required Headers:**
- No `Date:` header in responses (required by HTTP/1.1)
- No `Server:` header

## 🧪 Testing

### Basic Functionality Test
```bash
#!/bin/bash
# tests.sh

echo "Starting server tests..."

# Test 1: GET root
echo "Test 1: GET /"
curl -s http://localhost:4040/ | grep -q "Hello" && echo "✓ PASS" || echo "✗ FAIL"

# Test 2: GET specific file
echo "Test 2: GET /index.html"
curl -s -o /dev/null -w "%{http_code}" http://localhost:4040/index.html | grep -q "200" && echo "✓ PASS" || echo "✗ FAIL"

# Test 3: 404 for missing file
echo "Test 3: 404 for missing file"
curl -s -o /dev/null -w "%{http_code}" http://localhost:4040/nonexistent.html | grep -q "404" && echo "✓ PASS" || echo "✗ FAIL"

# Test 4: POST request
echo "Test 4: POST with body"
curl -s -X POST -d "user_name=sosa&user_age=300" http://localhost:4040/form.html | grep -q "processed" && echo "✓ PASS" || echo "✗ FAIL"

# Test 5: Keep-alive
echo "Test 5: Keep-alive connection"
RESULT=$(curl -v http://localhost:4040/ 2>&1 | grep -i "connection: keep-alive")
[ ! -z "$RESULT" ] && echo "✓ PASS" || echo "✗ FAIL"

echo "Tests complete!"
```

### Load Testing
```bash
# Using Apache Bench
ab -n 1000 -c 10 http://localhost:4040/

# Using wrk
wrk -t4 -c10 -d10s http://localhost:4040/

# Using siege
siege -c10 -t30s http://localhost:4040/
```

### Memory Leak Detection
```bash
# Compile with debugging symbols
gcc -g -o server server.c

# Run with Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./server

# In another terminal, make requests
curl http://localhost:4040/

# Check Valgrind output for leaks
```

### Security Testing
```bash
# Test path traversal (should be blocked)
curl http://localhost:4040/../../../etc/passwd
curl http://localhost:4040/..%2f..%2f..%2fetc%2fpasswd

# Test large request (current implementation will truncate)
python3 << 'EOF'
import socket
s = socket.socket()
s.connect(('localhost', 4040))
s.send(b'GET / HTTP/1.1\r\n' + b'X-Large: ' + b'A'*100000 + b'\r\n\r\n')
print(s.recv(1024))
s.close()
EOF
```

## 🔍 Troubleshooting

### Server Won't Start

**Error: "Binding failed: Address already in use"**
```bash
# Check if port 4040 is in use
lsof -i :4040
netstat -tlnp | grep 4040

# Kill existing process
kill -9 <PID>

# Or use a different port (edit server.c line 427)
```

**Error: "Cannot create socket: Permission denied"**
```bash
# Ports < 1024 require root privileges
# Solution: Use port > 1024 or run as root (not recommended)
sudo ./server
```

### Connection Issues

**"Connection refused"**
- Server not running: Start with `./server`
- Firewall blocking port: `sudo ufw allow 4040`
- Wrong IP/port: Verify with `netstat -tlnp`

**"Connection reset by peer"**
- Server crashed: Check server output for errors
- Too many connections: Wait for slots to free up
- Memory exhaustion: Check with `free -h`

### Performance Issues

**Server becomes unresponsive**
```bash
# Check process count
ps aux | grep server | wc -l

# Check system load
top  # Look for high CPU/memory usage

# Increase OPEN_MAX if needed (edit server.c line 16)
```

**Slow response times**
```bash
# Check available memory
free -h

# Reduce stack size to save memory
ulimit -s 1024

# Monitor with strace
strace -p <PID>
```

### Debugging

**Enable verbose output:**
```c
// Add more printf() statements in handle_method()
printf("DEBUG: Reading file: %s\n", full_path);
printf("DEBUG: File size: %ld bytes\n", file_size);
```

**Use GDB:**
```bash
# Compile with -g flag
gcc -g -o server server.c

# Run in debugger
gdb ./server

(gdb) run
(gdb) break handle_method  # Set breakpoint
(gdb) continue
(gdb) print full_path      # Inspect variables
```

**Check system logs:**
```bash
# Linux
journalctl -f

# macOS
log stream --predicate 'process == "server"'
```

## 📚 Learning Resources

### HTTP Protocol
- [RFC 7230 - HTTP/1.1 Message Syntax and Routing](https://tools.ietf.org/html/rfc7230)
- [RFC 7231 - HTTP/1.1 Semantics and Content](https://tools.ietf.org/html/rfc7231)
- [MDN HTTP Documentation](https://developer.mozilla.org/en-US/docs/Web/HTTP)

### Systems Programming
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [The Linux Programming Interface](https://man7.org/tlpi/)
- [Unix Network Programming - Stevens](https://www.amazon.com/Unix-Network-Programming-Sockets-Networking/dp/0131411551)

### Related Concepts
- Process vs Thread models
- Event-driven architecture (epoll, kqueue)
- Zero-copy techniques (sendfile, splice)
- HTTP/2 and HTTP/3 protocols

## 🤝 Contributing

This is an educational project. Improvements welcome:

### Suggested Enhancements

**High Priority:**
1. Implement proper path validation after canonicalization
2. Add request/response timeouts

**Medium Priority:**
5. Dynamic buffer sizing for large requests
6. Proper logging system
7. Configuration file support
8. More HTTP methods (PUT, DELETE, HEAD)

**Nice to Have:**
9. TLS/SSL support (OpenSSL integration)
10. Event-driven architecture (epoll/kqueue)
11. HTTP/2 support
12. Compression (gzip)

## 📄 License

This code is provided for educational purposes. Feel free to use, modify, and distribute for learning.

**Disclaimer:** This server is NOT production-ready. Use at your own risk. The authors assume no liability for any damage caused by using this software.

## 🎓 Educational Goals

This project demonstrates:
- ✅ Socket programming (TCP/IP)
- ✅ HTTP protocol implementation
- ✅ Process-based concurrency (fork)
- ✅ Inter-process communication (semaphores)
- ✅ Signal handling (SIGCHLD)
- ✅ File I/O operations
- ✅ String parsing and manipulation
- ✅ Memory management
- ✅ Error handling

### What's NOT Covered (Further Learning)
- Thread-based concurrency (pthreads)
- Event-driven I/O (epoll/kqueue/io_uring)
- Asynchronous I/O
- Zero-copy operations
- TLS/SSL encryption
- Advanced HTTP features (HTTP/2, WebSockets)
- Production-grade logging
- Configuration management
- Graceful shutdown and reload

---

**Project Status:** Educational Implementation  
**Last Updated:** December 2025  
**Maintainer:** Champ Like Bailey

For questions, issues, or suggestions, please open an issue on the project repository.

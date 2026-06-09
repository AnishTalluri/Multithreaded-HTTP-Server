# Multithreaded HTTP Server

A multithreaded HTTP/1.1 server written in C. It uses a fixed pool of worker
threads pulling from a shared, synchronized connection queue, and enforces
per-file read/write locking so that concurrent `GET` and `PUT` requests on the
same resource stay consistent. Every request is recorded to an audit log in a
total order that is coherent with the server's locking, so the log reflects a
valid serialization of concurrent operations.

## Features

- **Thread pool** — a configurable number of worker threads (default 4) handle
  connections concurrently; the dispatcher only accepts and enqueues.
- **Synchronized work queue** — connections are handed off to workers through a
  mutex/condition-variable queue.
- **Per-file locking** — a hashtable of read/write locks keyed by URI allows
  many simultaneous readers or a single writer per file, so requests to
  different files never block each other.
- **Atomic audit log** — each handled request appends a single
  `Operation,URI,Status,RequestId` line under a global log lock, giving a
  consistent linear history of all requests.
- **HTTP methods** — supports `GET` and `PUT`; unsupported methods return the
  appropriate status code.

## Building

Requires `clang` and `make` on a POSIX system (developed and tested on Linux).

```bash
make
```

This produces the `httpserver` executable. `make clean` removes build
artifacts; `make format` runs `clang-format` over the sources.

> **Note:** linking requires the course-provided helper library
> (`asgn4_helper_funcs.a`) and `asgn2_helper_funcs.h`. If these are not present,
> the server will not link.

## Usage

```bash
./httpserver [-t num_threads] <port>
```

- `-t num_threads` — size of the worker thread pool (default: 4)
- `<port>` — TCP port to listen on

Example:

```bash
./httpserver -t 8 8080
```

### Requests

```bash
# Store a file
curl -T myfile.txt http://localhost:8080/myfile.txt

# Retrieve it
curl http://localhost:8080/myfile.txt
```

## Project layout

```
httpserver.c        Server entry point, thread pool, dispatch, audit logging
hashtable.c/.h      URI -> lock hashtable
rwlock.h            Reader/writer lock interface
queue.h             Synchronized connection queue interface
connection.h        Connection object and request parsing
request.h           Request type definitions
response.h          Response codes and helpers
protocol.h          HTTP protocol constants
debug.h             Debug macros
test_files/         Sample files for testing
test_scripts/       Shell + Python test drivers
workloads/          TOML workload definitions for concurrency/audit tests
```

## Testing

The `test_scripts/` and `workloads/` directories contain concurrency and audit
tests that exercise conflicting and non-conflicting GET/PUT workloads under
load and verify the audit log is a valid serialization.

## Implementation notes

The dispatcher loops accepting connections and pushes each descriptor onto the
queue. Worker threads block on the condition variable until work is available,
pop a connection, parse the request, acquire the appropriate read or write lock
for the target URI, perform the operation, log it, and close the connection.
Locks are stored per-URI in the hashtable so unrelated files are handled fully
in parallel.

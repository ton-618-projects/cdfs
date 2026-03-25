# CDFS — Centralized Distributed File System

> A lightweight distributed file system implemented in C, built from the ground up to explore low-level file storage, metadata management, and networked chunk distribution.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [How It Works](#how-it-works)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build](#build)
  - [Run](#run)
  - [Clean](#clean)
- [API Reference](#api-reference)
- [Configuration](#configuration)
- [Testing](#testing)
- [Team](#team)
- [License](#license)

---

## Overview

**CDFS** is a simplified distributed file system written in pure C. It demonstrates the core concepts behind large-scale storage systems like HDFS and GFS — including file chunking, metadata tracking, and client-server data flow — within a compact, readable codebase.

The goal of this project is educational: to understand how real distributed systems manage files across multiple storage nodes, coordinate metadata, and serve clients reliably.

**Key capabilities:**

- Upload a local file into the distributed file system (`cdfs_put`)
- Retrieve a file from the distributed file system to local disk (`cdfs_get`)
- File data is split into chunks and managed by dedicated storage nodes
- A central metadata server tracks file-to-chunk mappings
- Entirely implemented in **C11** with no external dependencies

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        CLIENT                           │
│                  (cdfs_put / cdfs_get)                  │
└──────────────────────────┬──────────────────────────────┘
                           │ 1. Metadata request
                           ▼
┌─────────────────────────────────────────────────────────┐
│                   METADATA SERVER                       │
│          Tracks file names → chunk locations            │
│          Assigns chunk IDs for new uploads              │
└──────────┬──────────────────────────────────────────────┘
           │ 2. Chunk location info
           ▼
┌─────────────────────────────────────────────────────────┐
│                    STORAGE NODE(S)                      │
│         Stores raw file chunks as chunk_*.dat           │
│         Serves chunk data back on retrieval             │
└─────────────────────────────────────────────────────────┘
```

**Data flow — Upload (`cdfs_put`):**

1. Client reads the local file and splits it into fixed-size chunks.
2. Client contacts the Metadata Server to register the file and obtain chunk IDs.
3. Client writes each chunk to the Storage Node as a `chunk_<id>.dat` file.
4. Metadata Server records the mapping of `dfs_path → [chunk_id_1, chunk_id_2, ...]`.

**Data flow — Download (`cdfs_get`):**

1. Client asks the Metadata Server for the chunk list associated with the given DFS path.
2. Client reads each chunk from the Storage Node in order.
3. Client reassembles the chunks into the output file on local disk.

---

## Project Structure

```
cdfs/
├── client/
│   ├── cli.c
│   ├── dfs_client.c       
│   └── client.h
├── common/
│   ├── dfs.h          
│   ├── protocol.h
│   ├── serialization.c
│   ├── serialization.h
│   └── utils.c
├── metadata_server/
│   ├── heartbeat.c
│   ├── metadata_server.c
│   ├── metadata.c         
│   └── metadata.h
├── scripts/ 
│   ├──kill_cluster.sh
│   └──start_cluster.sh               
├── storage_node/
│   ├── chunk_store.c           
│   ├── storage_node.c
│   └── storage.c
├── tests/  
│   ├── test_chunking.c
│   └── test_metadata.c  
├── .gitignore             
├── LICENSE                 
├── main.c                  
├── Makefile               
└── README.md
```

---

## How It Works

### Chunking

Files are broken into fixed-size **64 KB chunks** (`CHUNK_SIZE = 64 * 1024`) before being stored. Each chunk is persisted by the storage layer as a separate `chunk_<id>.dat` file on disk. This mirrors the design of real distributed systems where large files are split to enable parallelism and fault tolerance. A single file can span up to **1024 chunks** (`MAX_CHUNKS`), meaning the system supports files up to **64 MB** in size out of the box.

The storage layer exposes two low-level primitives for this:

```c
// Write a chunk to disk; returns the assigned chunk_id (≥ 0) or -1 on error
int32_t store_chunk(const uint8_t *data, size_t size);

// Read a previously stored chunk into a caller-supplied buffer
int32_t load_chunk(int32_t chunk_id, uint8_t *buffer, size_t buffer_size, size_t *bytes_read);
```

`store_chunk` accepts a raw byte array and its size, writes it to disk, and returns a unique `chunk_id`. `load_chunk` takes that ID along with a pre-allocated buffer, fills it with the chunk data, and sets `*bytes_read` to the actual number of bytes loaded — which may be less than `CHUNK_SIZE` for the final chunk of a file.

### Metadata Management

The metadata server maintains an in-memory registry (bounded by `MAX_FILES = 128` entries) that maps DFS paths (e.g., `/dfs/input.txt`) to file metadata, including the ordered list of chunk IDs, file permissions, timestamps, and total size.

```c
// Register a new file in the metadata registry
int32_t register_file(
    const char        *filename,      // DFS path (e.g. "/dfs/input.txt")
    const chunk_info_t *chunks,       // Array of chunk descriptors
    int32_t            chunk_count,   // Number of chunks
    uint32_t           file_mode,     // Unix-style file permissions (e.g. 0644)
    int64_t            created_at,    // Creation timestamp (Unix epoch)
    int64_t            modified_at,   // Last-modified timestamp (Unix epoch)
    uint64_t           file_size      // Total file size in bytes
);

// Retrieve metadata for a previously registered file
int32_t get_file_metadata(const char *filename, file_metadata_t *out);
```

`register_file` is called during `cdfs_put` after all chunks have been stored — it atomically commits the full file record so that partial uploads are never visible to readers. `get_file_metadata` populates a `file_metadata_t` struct with everything needed to reconstruct the file: the chunk ID list, count, size, and timestamps.

### Client Layer

The client is the only layer a user of CDFS directly interacts with. It orchestrates the storage and metadata layers transparently:

```c
int32_t cdfs_put(const char *local_path, const char *cdfs_path);
int32_t cdfs_get(const char *cdfs_path, const char *local_path);
```

During `cdfs_put`, the client reads the local file in `CHUNK_SIZE` increments, calls `store_chunk` for each piece, collects the returned chunk IDs, then calls `register_file` to commit the mapping. During `cdfs_get`, it calls `get_file_metadata` to retrieve the chunk list, calls `load_chunk` for each ID in order, and writes the reassembled bytes to the output file.

---

## Getting Started

### Prerequisites

- **GCC** (or any C11-compatible compiler)
- **GNU Make**
- Linux / macOS (POSIX environment)

```bash
# Verify your toolchain
gcc --version
make --version
```

### Build

Clone the repository and compile with a single command:

```bash
git clone https://github.com/ton-618-projects/cdfs.git
cd cdfs
make
```

This produces the `cdfs` binary in the project root.

### Run

The default `main.c` demonstrates a full upload-download round trip:

```bash
# Create a test file
echo "Hello, distributed world!" > input.txt

# Run the DFS demo
./cdfs
```

Expected output:

```
DFS test completed
```

After execution, `output.txt` will contain the contents retrieved from `/dfs/input.txt` — matching the original `input.txt`.

### Clean

Remove all build artifacts and chunk data files:

```bash
make clean
```

---

## API Reference

CDFS exposes three layers of API, each building on the one below it. Normal users only need the **Client API**; the Storage and Metadata APIs are internal but documented here for contributors and reviewers.

---

### Storage Layer — `storage_node/storage.h`

#### `store_chunk`

```c
int32_t store_chunk(const uint8_t *data, size_t size);
```

Writes a chunk of raw bytes to disk and returns its assigned `chunk_id`.

| Parameter | Type              | Description                                      |
|-----------|-------------------|--------------------------------------------------|
| `data`    | `const uint8_t *` | Pointer to the byte buffer containing chunk data |
| `size`    | `size_t`          | Number of bytes to write (≤ `CHUNK_SIZE`)        |

**Returns:** A non-negative `chunk_id` on success, `-1` on failure.

The chunk is persisted as `chunk_<id>.dat` in the storage directory. The ID is monotonically increasing and unique per CDFS instance.

---

#### `load_chunk`

```c
int32_t load_chunk(int32_t chunk_id, uint8_t *buffer, size_t buffer_size, size_t *bytes_read);
```

Reads a previously stored chunk from disk into a caller-supplied buffer.

| Parameter     | Type        | Description                                                               |
|---------------|-------------|---------------------------------------------------------------------------|
| `chunk_id`    | `int32_t`   | ID returned by a prior `store_chunk` call                                 |
| `buffer`      | `uint8_t *` | Pre-allocated memory block where chunk data will be copied                |
| `buffer_size` | `size_t`    | Capacity of `buffer` in bytes — should be at least `CHUNK_SIZE`           |
| `bytes_read`  | `size_t *`  | Output: set to the actual number of bytes loaded (may be < `CHUNK_SIZE` for the last chunk) |

**Returns:** `0` on success, `-1` if the chunk does not exist or a read error occurs.

> **Note:** Always allocate `buffer` with at least `CHUNK_SIZE` bytes. The final chunk of a file is often smaller — use `*bytes_read` rather than `buffer_size` when writing the reassembled file.

---

### Metadata Layer — `metadata_server/metadata.h`

#### `register_file`

```c
int32_t register_file(
    const char         *filename,
    const chunk_info_t *chunks,
    int32_t             chunk_count,
    uint32_t            file_mode,
    int64_t             created_at,
    int64_t             modified_at,
    uint64_t            file_size
);
```

Registers a new file entry in the metadata registry, associating a DFS path with its chunk list and file attributes.

| Parameter     | Type                    | Description                                                    |
|---------------|-------------------------|----------------------------------------------------------------|
| `filename`    | `const char *`          | DFS path of the file (e.g. `"/dfs/input.txt"`, max `MAX_FILENAME` chars) |
| `chunks`      | `const chunk_info_t *`  | Array of chunk descriptors (chunk IDs and sizes)               |
| `chunk_count` | `int32_t`               | Length of the `chunks` array (max `MAX_CHUNKS`)                |
| `file_mode`   | `uint32_t`              | Unix-style permission bits (e.g. `0644`)                       |
| `created_at`  | `int64_t`               | File creation time as a Unix epoch timestamp                   |
| `modified_at` | `int64_t`               | Last modification time as a Unix epoch timestamp               |
| `file_size`   | `uint64_t`              | Total size of the file in bytes                                |

**Returns:** `0` on success, `-1` if the registry is full (`MAX_FILES` reached) or the filename is invalid.

This call is made **after** all chunks have been successfully stored, ensuring the file is only visible once fully written.

---

#### `get_file_metadata`

```c
int32_t get_file_metadata(const char *filename, file_metadata_t *out);
```

Retrieves the full metadata record for a registered file.

| Parameter  | Type               | Description                                          |
|------------|--------------------|------------------------------------------------------|
| `filename` | `const char *`     | DFS path to look up                                  |
| `out`      | `file_metadata_t *`| Output struct populated with chunk list and attributes|

**Returns:** `0` on success, `-1` if the file is not found in the registry.

The populated `file_metadata_t` contains the ordered chunk ID array, chunk count, file size, permissions, and timestamps — everything needed by the client to reconstruct the original file.

---

### Client API — `client/dfs_client.h`

#### `cdfs_put`

```c
int32_t cdfs_put(const char *local_path, const char *cdfs_path);
```

Uploads a local file into the distributed file system.

| Parameter    | Type           | Description                                          |
|-------------|----------------|------------------------------------------------------|
| `local_path` | `const char *` | Path to the source file on local disk                |
| `cdfs_path`  | `const char *` | Destination path inside the CDFS namespace           |

**Returns:** `0` on success, `-1` on failure (file not found, registry full, storage error, etc.).

Internally: opens `local_path`, reads it in `CHUNK_SIZE` blocks, calls `store_chunk` for each block, then calls `register_file` with the collected chunk IDs and file attributes.

---

#### `cdfs_get`

```c
int32_t cdfs_get(const char *cdfs_path, const char *local_path);
```

Downloads a file from the distributed file system to local disk.

| Parameter    | Type           | Description                                          |
|-------------|----------------|------------------------------------------------------|
| `cdfs_path`  | `const char *` | Source path inside the CDFS namespace                |
| `local_path` | `const char *` | Destination path on local disk                       |

**Returns:** `0` on success, `-1` on failure (file not registered, chunk missing, write error, etc.).

Internally: calls `get_file_metadata` to retrieve the chunk list, then calls `load_chunk` for each chunk ID in order, writing the reassembled bytes to `local_path`.

---

## Configuration

All tuneable constants are defined in `common/dfs.h`. Modify them and recompile to change system limits.

```c
#define CHUNK_SIZE   (64 * 1024)  // Each file is divided into 64 KB chunks
#define MAX_CHUNKS   1024         // Max chunks per file → supports files up to 64 MB
#define MAX_FILES    128          // Max number of files tracked in the metadata registry
#define MAX_FILENAME 256          // Maximum length of a DFS file path (bytes)
```

| Constant        | Value       | Effect of changing it                                                                 |
|----------------|-------------|----------------------------------------------------------------------------------------|
| `CHUNK_SIZE`    | `65536` (64 KB) | Larger = fewer chunks per file, less metadata overhead, more memory per `load_chunk` buffer. Smaller = more granular storage, higher chunk count. |
| `MAX_CHUNKS`    | `1024`      | Directly controls the maximum file size: `MAX_CHUNKS × CHUNK_SIZE`. At defaults, max file = **64 MB**. Double `MAX_CHUNKS` to support 128 MB files. |
| `MAX_FILES`     | `128`       | Total number of distinct files the metadata registry can hold. Increase if you need to store more files simultaneously. |
| `MAX_FILENAME`  | `256`       | Maximum byte length of a DFS path string including the null terminator. Increase if you need deeply nested paths. |

> **Tip:** After changing any of these constants, run `make clean && make` to ensure all object files are rebuilt with the new values.

---

## Testing

Test cases live in the `tests/` directory. To run them:

```bash
cd tests
# Run individual test scripts or executables as per test directory contents
```

Suggested test scenarios:

- **Small file:** Verify a file smaller than one chunk is stored and retrieved correctly.
- **Large file:** Verify a multi-chunk file is reassembled in the correct order.
- **Non-existent file:** Confirm `cdfs_get` returns an error for unknown DFS paths.
- **Repeated put:** Confirm behavior when uploading to an already-registered DFS path.

---


## License

This project is licensed under the [MIT License](LICENSE).

```
MIT License — free to use, modify, and distribute with attribution.
```

---

<p align="center">
  Built  in C &nbsp;|&nbsp; No external dependencies &nbsp;|&nbsp; Educational use
</p>
# Curren-memory

Curren-memory is a small, dependency-free, single-threaded C++20 key-value store for arbitrary byte sequences, with an arena-backed open-addressing index and an append-only write-ahead log. It has no thread synchronization, network interface, or TTL/expiration support; callers must serialize access and treat returned value spans as short-lived views.

## Memory and log layouts

Each entry is one arena allocation. Lengths count bytes and do not imply strings or terminators:

```text
Entry
+----------+----------+----------------+----------------+
| u32 key  | u32 val  | key bytes ...  | value bytes ...|
+----------+----------+----------------+----------------+
             key_len      key_len bytes    val_len bytes
```

The append-only log uses little-endian fixed-width integers. CRC-32 (IEEE) covers the header and payload, excluding the trailing checksum:

```text
+----------+------+----------+----------+-------------+---------------+----------+
| u32 magic| u8 op| u32 key  | u32 value| key bytes   | value bytes  | u32 CRC  |
+----------+------+----------+----------+-------------+---------------+----------+
                     key_len    val_len
```

`op` is 1 for PUT and 2 for DEL; DEL records have `val_len = 0`.

## Build, test, and run

```sh
cmake -S . -B build -DCURREN_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/curren-cli store.log
```

Disable sanitizers with `-DCURREN_ENABLE_SANITIZERS=OFF` for a normal build.
The CLI accepts `put`, `get`, `del`, `compact`, and `quit`; byte input is hexadecimal
(`hex:...`) or literal text (`text:...`).

## API notes

Keys and values are `std::span<const std::byte>` at the API boundary. Values returned
by `get` remain valid only until the next mutating call on that store. The implementation
is single-threaded and has no locks.

## Known limitations and next steps

The log is a single file and recovery truncates at the first malformed or incomplete
record. The arena retains replaced/deleted entries until compaction or reset, and table
operations are single-threaded. Possible next steps include an mmap-backed arena,
Robin Hood hashing, checksummed segments, and multi-file log rotation.

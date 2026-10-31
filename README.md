# Project: Curren-memory

A small embeddable key-value store in modern C++ with byte-oriented keys and values,
an arena-backed open-addressing index, and a crash-safe append-only log.

This is a portfolio-grade systems project. Reviewers will read the code, the design doc,
the tests, and the commit history. Optimize for correctness, clarity, and honest
engineering, not feature count.

## Hard requirements
- Language: C++20. Standard library only at runtime. Test/bench tooling may use
  CMake FetchContent only if unavoidable; prefer none.
- Build: CMake >= 3.20, target-based (`target_include_directories`, `target_compile_features`),
  exports an installable `curren::curren` target, and builds as a static library by default.
- Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on GCC/Clang, `/W4` on MSVC.
  Zero warnings. Warnings are errors in CI.
- Platforms: Linux and macOS required; Windows is out of scope for file durability
  (document why, and isolate POSIX calls in one translation unit).
- No global state. No `using namespace` in headers. Everything lives in namespace `curren`.
- No raw owning pointers outside the arena. Follow the Rule of Zero/Five deliberately.
- Error handling: no exceptions in library code. Use `curren::Result<T>` (a minimal
  expected-like type) with an `Errc` enum. Never ignore a syscall return value.
- Threading: single-writer, single-threaded. State this in the API docs and DESIGN.md.

## Design

### Arena (`arena.hpp/.cpp`)
- Chunked bump allocator; chunks never move, so pointers stay stable until `reset()`.
- Correct alignment handling, overflow-checked size arithmetic, oversize requests get a dedicated chunk.
- Expose `bytes_used()`, `bytes_reserved()`.

### Index (`table.hpp/.cpp`)
- Open addressing, linear probing, power-of-two capacity, tombstones, rehash at load factor 0.7.
- Entry stored as one contiguous arena allocation: header { u32 key_len, u32 val_len } + key + value.
- Hash function behind a small template/policy seam (default FNV-1a 64). Test collisions
  by injecting a deliberately bad hash.
- Document probe-length behavior and the tombstone trade-off in DESIGN.md.
- Treat keys as unsigned bytes; embedded `\0`, empty keys, and empty values are valid.

### Log (`log.hpp/.cpp`)
- Append-only, little-endian, fixed-width record format:
  `[u32 magic][u8 version][u8 op][u32 key_len][u32 val_len][key][value][u32 crc32c]`
- Define max key/value sizes and reject oversize input before writing.
- Recovery: replay on open; on truncated or CRC-bad tail, stop, truncate to the last
  good offset, and report what was recovered via `OpenReport`.
- Durability: `Options::sync_policy` = `Always | OnRequest`. Implement with `write` + `fsync`
  (and directory `fsync` after rename). Describe exactly what is and is not guaranteed.
- Compaction: write live entries to `<path>.tmp`, `fsync`, `rename`, `fsync` parent dir,
  then swap state. Must be safe against a crash at every step.

### Public API (`curren.hpp`)
```cpp
class Store {
 public:
  static Result<Store> open(const std::filesystem::path&, Options = {});
  Result<void> put(std::span<const std::byte> key, std::span<const std::byte> value);
  std::optional<std::span<const std::byte>> get(std::span<const std::byte> key) const noexcept;
  Result<bool> erase(std::span<const std::byte> key);
  std::size_t size() const noexcept;
  Result<void> compact();
  Result<void> sync();
  Stats stats() const noexcept;   // live bytes, wasted bytes, probe stats, log size
};
```
Document span lifetime (valid until the next mutating call), move-only semantics,
and exception safety guarantees on every public function.

## Repository layout
```
Curren-memory/
  CMakeLists.txt
  cmake/                    # warnings, sanitizers, install config
  include/curren/           # public headers only
  src/
  tests/                    # unit tests, plain assert-based mini runner (no framework)
  fuzz/                     # libFuzzer harness for the log parser
  bench/                    # microbenchmarks
  examples/cli.cpp
  docs/DESIGN.md
  .github/workflows/ci.yml
  .clang-format  .clang-tidy  .gitignore  LICENSE (MIT)  CHANGELOG.md  README.md
```

## Quality gates
1. **Tests:** arena (alignment, growth, stability, reset), table (CRUD, collisions, rehash,
   binary keys, empty key/value), log (round-trip, truncated tail, flipped-bit CRC,
   crash-during-compaction simulation by truncating/aborting at each step),
   plus a randomized differential test against `std::map<std::vector<std::byte>, std::vector<std::byte>>`
   over 100k+ operations with a fixed seed.
2. **Sanitizers:** the full suite passes under ASan+UBSan. Provide a CMake option
   `CURREN_SANITIZE=ON`.
3. **Fuzzing:** a libFuzzer target that feeds arbitrary bytes to log replay. It must never
   crash, hang, or over-read. Include a seed corpus.
4. **Static analysis:** `.clang-tidy` enabled (bugprone, performance, modernize,
   cppcoreguidelines subsets) and clean. `clang-format` enforced in CI.
5. **CI (GitHub Actions):** matrix of gcc and clang on ubuntu, clang on macOS; jobs for
   build+test, sanitizers, clang-tidy, and format check.
6. **Benchmarks:** put/get/erase, sequential and random keys, 16B/128B/1KiB values, versus
   `std::unordered_map<std::string, std::string>`. Report real numbers from this machine
   in the README with hardware, compiler, and flags. Never invent or estimate figures.
   If Curren is slower somewhere, say so and explain why.

## Documentation
- **README.md:** what it is, why it exists, a 15-line usage example, build/test/bench
  commands, feature list, explicit non-goals (no concurrency, networking, TTL, or transactions),
  benchmark table, and known limitations. No marketing language, no emojis, no filler.
- **docs/DESIGN.md:** memory layout diagrams (ASCII), on-disk format spec, invariants,
  recovery algorithm, the durability argument, alternatives considered and rejected
  (e.g. chaining vs. open addressing, mmap vs. write/fsync), and future work
  (Robin Hood hashing, segmented log, mmap-backed arena).
- Public headers get concise doc comments stating preconditions, postconditions,
  and error cases. Comments explain why, not what.

## Process rules
1. First, reply with a short plan: file list, key data structures, and risks. Wait for
   my approval before writing code.
2. Implement module by module. After each module, build and run tests with and without
   sanitizers and report the results.
3. Small functions, no dead code, no TODOs left in committed code, no unexplained magic numbers.
4. Do not claim something works unless you ran it. Paste real command output.
5. Never run `git push` or alter the remote without asking me first.

## Git history (reviewers read this)
Use Conventional Commits, one logical change per commit, each commit building and passing tests:
```
chore: scaffold CMake project, license, formatting and lint config
feat(arena): add chunked bump allocator with tests
feat(table): add open-addressing index with tombstones and rehash
feat(log): add CRC-checked append-only log with crash recovery
feat(store): add public API and compaction
test: add differential and corruption tests
test(fuzz): add libFuzzer harness for log replay
perf(bench): add benchmarks and publish results
ci: add GitHub Actions matrix, sanitizers, clang-tidy
docs: add DESIGN.md and finalize README
```
Remote: https://github.com/FRYUR/Curren-memory.git, branch `main`.
Ask for confirmation, then run `git push -u origin main`.

## Definition of done
Fresh clone, then `cmake -B build && cmake --build build && ctest --test-dir build` passes with
zero warnings; sanitizer build and fuzz target run clean; CI is green; README benchmark
numbers are reproducible from the documented commands.

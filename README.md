# kernel-io


```
══════════════════ SENDER ══════════════════

  ┌─────────────────────────┐
  │       Application       │
  └────────────┬────────────┘
               │  write() / send()
  ┌────────────▼────────────┐
  │   Socket Send Buffer    │◄── epoll/kqueue/io_uring
  └────────────┬────────────┘    (notify when ready)
               │
  ┌────────────▼────────────┐
  │     TCP (segmentation)  │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │     IP (routing)        │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │   Netfilter (iptables)  │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │    QDISC (tc policy)    │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │     TX Queue → NIC      │
  └────────────┬────────────┘
               │
            ~~~│~~~ wire / air
               │
══════════════════ RECEIVER ══════════════════
               │
  ┌────────────▼────────────┐
  │     NIC → RX Queue      │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │   Netfilter (iptables)  │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │     IP (decapsulate)    │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │ TCP (reorder + ACK)     │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │  Socket Receive Buffer  │──► epoll/kqueue/io_uring
  └────────────┬────────────┘    (notify the app)
               │  read() / recv()
  ┌────────────▼────────────┐
  │       Application       │
  └─────────────────────────┘
```

Experiments with the TCP stack in C++20, comparing four multiplexed I/O backends:

| Backend  | Syscall    | Platform                  |
|----------|------------|---------------------------|
| `simple` | `select`   | any (POSIX)               |
| `epoll`  | `epoll`    | **Linux only**            |
| `kqueue` | `kqueue`   | **macOS / BSD only**      |
| `uring`  | `io_uring` | **Linux only** (≥ 5.1)    |

Each backend is compiled as an **independent executable** (`kernel-io-simple`, `kernel-io-epoll`, `kernel-io-kqueue`, `kernel-io-uring`). CMake only configures the backends supported by the current platform — manually enabling an incompatible backend fails at configure time.

## Prerequisites

- CMake ≥ 3.21
- A C++20 compiler (clang 14+, gcc 11+, AppleClang 14+)
- [Conan 2](https://conan.io/) (`pip install "conan>=2.0"` then `conan profile detect --force`)
- `make` (ships with macOS/Linux)
- Docker (optional, only needed to build Linux backends in a container)

## Quick build

The `Makefile` at the root orchestrates `conan install` + `cmake configure` + `cmake build` for you:

```bash
make build              # debug, all backends compatible with the current platform
make build BUILD_TYPE=Release
make clean
```

On a macOS machine this compiles `kernel-io-simple` and `kernel-io-kqueue`. On a Linux machine it compiles `kernel-io-simple`, `kernel-io-epoll`, and `kernel-io-uring`.

### Running the binaries

```bash
make run-simple         # runs on any platform
make run-kqueue         # macOS / BSD
make run-epoll          # Linux
make run-uring          # Linux (io_uring, kernel ≥ 5.1)
```

> Design decision: since each backend is a separate binary, "selecting a backend" simply means "which binary you invoke". There is no `--epoll`/`--kqueue` runtime flag.

### Debugging the binaries

```bash
make debug-simple       # any platform
make debug-kqueue       # macOS / BSD (uses lldb)
make debug-epoll        # Linux (uses gdb)
make debug-uring        # Linux (uses gdb)

# override the debugger manually
make debug-kqueue DEBUGGER="lldb --"
```

The `debug-*` targets depend on `build`, so the binary is recompiled if necessary before entering the debugger. `BUILD_TYPE` defaults to `Debug`, so symbols are present without any extra configuration.

## Manual build (without the Makefile)

If you prefer to run the commands directly:

```bash
# 1. Conan downloads dependencies and generates the CMake toolchain
#    (cmake_layout places everything under build/Debug/generators automatically)
conan install . --build=missing -s build_type=Debug

# 2. Configure CMake using the preset that points to the Conan toolchain
cmake --preset debug

# 3. Build
cmake --build build/Debug
```

Binaries are placed at `build/Debug/src/<backend>/kernel-io-<backend>`.

### Selecting backends manually

```bash
# disable simple and keep only kqueue (on macOS)
cmake --preset debug -DKIO_BUILD_SIMPLE=OFF -DKIO_BUILD_KQUEUE=ON

# build a single target
cmake --build build/Debug --target kernel-io-kqueue

# build only the io_uring backend (Linux)
cmake --preset debug -DKIO_BUILD_URING=ON
cmake --build build/Debug --target kernel-io-uring
```

Forcing an incompatible backend fails early:

```bash
cmake --preset debug -DKIO_BUILD_EPOLL=ON
# CMake Error: KIO_BUILD_EPOLL=ON requires Linux (current: Darwin).

cmake --preset debug -DKIO_BUILD_URING=ON
# CMake Error: KIO_BUILD_URING=ON requires Linux (current: Darwin).
```

## Docker (Linux backends only)

The `simple`, `epoll`, and `uring` backends run in a container. **`kqueue` cannot be containerized** — Docker only provides Linux containers, and `kqueue` requires a BSD kernel. For `kqueue`, run natively on a macOS/BSD host.

```bash
make docker-epoll       # builds kernel-io:epoll
make docker-simple      # builds kernel-io:simple
make docker-uring       # builds kernel-io:uring

docker run --rm kernel-io:epoll
docker run --rm kernel-io:simple
docker run --rm kernel-io:uring
```

The `Dockerfile` is multi-stage: the first stage has `cmake`, `conan`, and the build toolchain; the second is a lean `debian:bookworm-slim` that receives only the binary and runs as the unprivileged user `kio`.

## Project layout

```
kernel-io/
├── CMakeLists.txt              # root: project(), find_package, subdirs
├── CMakePresets.json           # debug/release presets
├── Makefile                    # conan + cmake wrapper
├── conanfile.txt               # spdlog, gtest
├── Dockerfile                  # multi-stage, ARG BACKEND={epoll,simple}
├── cmake/                      # reusable CMake modules
│   ├── PreventInSourceBuilds.cmake
│   ├── StandardProjectSettings.cmake
│   ├── ProjectOptions.cmake    # KIO_BUILD_* toggles + platform guards
│   ├── CompilerWarnings.cmake  # kio_set_warnings(target) function
│   └── StaticAnalysers.cmake   # clang-tidy / cppcheck toggles
├── src/
│   ├── simple/                 # select() backend, portable
│   ├── epoll/                  # Linux epoll backend
│   ├── kqueue/                 # macOS/BSD backend
│   └── uring/                  # Linux io_uring backend (kernel ≥ 5.1)
└── tests/                      # gtest (scaffolding ready, no active tests)
```

## Tests

The GoogleTest infrastructure is already wired up via Conan, but no tests are active. To add one:

1. Create `tests/test_<backend>.cpp` (e.g. `tests/test_simple.cpp`).
2. In [tests/CMakeLists.txt](tests/CMakeLists.txt), uncomment the corresponding line.
3. `make build && ctest --preset debug`.

---

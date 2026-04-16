# syntax=docker/dockerfile:1.7
#
# kernel-io: container build for the Linux backends (simple, epoll).
# kqueue is intentionally NOT supported here -- it requires a BSD kernel
# and Docker only runs Linux containers. Run kqueue natively on macOS/BSD.
#
# Build:
#   docker build --build-arg BACKEND=epoll  -t kernel-io:epoll  .
#   docker build --build-arg BACKEND=simple -t kernel-io:simple .
#
# Run:
#   docker run --rm kernel-io:epoll
#   docker run --rm kernel-io:simple

ARG BACKEND=epoll

# ---------- Stage 1: builder ----------
FROM debian:bookworm AS builder
ARG BACKEND

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      python3-pip \
      git \
      ca-certificates \
      liburing-dev \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --break-system-packages "conan>=2.0"

WORKDIR /src

# Copy only dependency-defining files first so the conan install layer
# stays cached until they actually change.
COPY conanfile.txt CMakePresets.json ./
COPY cmake/ ./cmake/

RUN conan profile detect --force \
    && conan install . \
        --build=missing \
        -s build_type=Release

# Now bring in the rest of the sources and build only the requested backend.
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY tests/ ./tests/

RUN cmake --preset release \
      -DKIO_BUILD_SIMPLE=$([ "$BACKEND" = "simple" ] && echo ON || echo OFF) \
      -DKIO_BUILD_EPOLL=$([ "$BACKEND"  = "epoll"  ] && echo ON || echo OFF) \
      -DKIO_BUILD_URING=$([ "$BACKEND"  = "uring"  ] && echo ON || echo OFF) \
      -DKIO_BUILD_KQUEUE=OFF \
      -DKIO_ENABLE_TESTS=OFF \
    && cmake --build build/Release --target kernel-io-${BACKEND}

# ---------- Stage 2: runtime ----------
FROM debian:bookworm-slim AS runtime
ARG BACKEND

RUN apt-get update && apt-get install -y --no-install-recommends \
      libstdc++6 \
      liburing2 \
      strace \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin kio

COPY --from=builder /src/build/Release/src/${BACKEND}/kernel-io-${BACKEND} /usr/local/bin/kernel-io

USER kio
ENTRYPOINT ["/usr/local/bin/kernel-io"]

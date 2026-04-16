# Convenience wrapper around conan + cmake.
# The "real" build system is CMake; this Makefile only orchestrates the
# conan install -> cmake configure -> cmake build sequence so you don't
# have to remember it.

BUILD_TYPE ?= Debug
BUILD_DIR  := build/$(BUILD_TYPE)
PRESET     := $(shell echo $(BUILD_TYPE) | tr A-Z a-z)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  DEBUGGER ?= lldb --
else
  DEBUGGER ?= gdb --args
endif

.DEFAULT_GOAL := build

.PHONY: help
help:
	@echo "kernel-io build targets:"
	@echo "  make build           # conan install + cmake configure + cmake build (Debug)"
	@echo "  make build BUILD_TYPE=Release"
	@echo "  make configure       # only run conan install + cmake configure"
	@echo "  make clean           # remove build/ entirely"
	@echo "  make run-simple      # run the simple backend binary"
	@echo "  make run-kqueue      # run the kqueue backend binary (macOS/BSD only)"
	@echo "  make run-epoll       # run the epoll backend binary (Linux only)"
	@echo "  make debug-simple    # run the simple backend under lldb/gdb"
	@echo "  make debug-kqueue    # run the kqueue backend under lldb (macOS/BSD)"
	@echo "  make debug-epoll     # run the epoll backend under gdb (Linux)"
	@echo "  make docker-epoll    # docker build BACKEND=epoll  -> kernel-io:epoll"
	@echo "  make docker-simple   # docker build BACKEND=simple -> kernel-io:simple"

.PHONY: deps
deps:
	conan install . \
		--build=missing \
		-s build_type=$(BUILD_TYPE)

.PHONY: configure
configure: deps
	cmake --preset $(PRESET)

.PHONY: build
build: configure
	cmake --build $(BUILD_DIR)

.PHONY: clean
clean:
	rm -rf build/

.PHONY: run-simple
run-simple:
	$(BUILD_DIR)/src/simple/kernel-io-simple

.PHONY: run-kqueue
run-kqueue:
	$(BUILD_DIR)/src/kqueue/kernel-io-kqueue

.PHONY: run-epoll
run-epoll:
	$(BUILD_DIR)/src/epoll/kernel-io-epoll

.PHONY: debug-simple
debug-simple: build
	$(DEBUGGER) $(BUILD_DIR)/src/simple/kernel-io-simple

.PHONY: debug-kqueue
debug-kqueue: build
	$(DEBUGGER) $(BUILD_DIR)/src/kqueue/kernel-io-kqueue

.PHONY: debug-epoll
debug-epoll: build
	$(DEBUGGER) $(BUILD_DIR)/src/epoll/kernel-io-epoll

.PHONY: docker-epoll
docker-epoll:
	docker build --build-arg BACKEND=epoll -t kernel-io:epoll .

.PHONY: docker-simple
docker-simple:
	docker build --build-arg BACKEND=simple -t kernel-io:simple .

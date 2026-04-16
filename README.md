# kernel-io


```
══════════════════ REMETENTE ══════════════════

  ┌─────────────────────────┐
  │       Application       │
  └────────────┬────────────┘
               │  write() / send()
  ┌────────────▼────────────┐
  │   Socket Send Buffer    │◄── epoll/kqueue/io_uring
  └────────────┬────────────┘    (notificam quando pronto)
               │
  ┌────────────▼────────────┐
  │     TCP (segmentação)   │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │     IP (roteamento)     │
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
            ~~~│~~~ fio / ar
               │
══════════════════ DESTINATÁRIO ══════════════════
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
  │     IP (desencapsula)   │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │ TCP (reorder + ACK)     │
  └────────────┬────────────┘
               │
  ┌────────────▼────────────┐
  │  Socket Receive Buffer  │──► epoll/kqueue/io_uring
  └────────────┬────────────┘    (notificam a app)
               │  read() / recv()
  ┌────────────▼────────────┐
  │       Application       │
  └─────────────────────────┘
```

Experimentos com pilha TCP em C++20, comparando três backends de I/O multiplexado:

| Backend  | Syscall   | Plataforma                |
|----------|-----------|---------------------------|
| `simple` | `select`  | qualquer (POSIX)          |
| `epoll`  | `epoll`   | **Linux apenas**          |
| `kqueue` | `kqueue`  | **macOS / BSD apenas**    |

Cada backend é compilado como **um executável independente** (`kernel-io-simple`, `kernel-io-epoll`, `kernel-io-kqueue`). O CMake só configura os backends suportados pela plataforma atual — tentar habilitar manualmente um backend incompatível falha em tempo de configuração.

## Pré-requisitos

- CMake ≥ 3.21
- Um compilador C++20 (clang 14+, gcc 11+, AppleClang 14+)
- [Conan 2](https://conan.io/) (`pip install "conan>=2.0"` e depois `conan profile detect --force`)
- `make` (já vem no macOS/Linux)
- Docker (opcional, só se for buildar os backends Linux em container)

## Build rápido

O `Makefile` na raiz orquestra `conan install` + `cmake configure` + `cmake build` para você:

```bash
make build              # debug, todos os backends compatíveis com a plataforma
make build BUILD_TYPE=Release
make clean
```

Em uma máquina macOS, isso compila `kernel-io-simple` e `kernel-io-kqueue`. Em uma máquina Linux, compila `kernel-io-simple` e `kernel-io-epoll`.

### Rodando os binários

```bash
make run-simple         # roda em qualquer plataforma
make run-kqueue         # macOS / BSD
make run-epoll          # Linux
```

> Decisão de design: como cada backend é um binário separado, "selecionar o backend" é simplesmente "qual binário você invoca". Não há flag `--epoll`/`--kqueue` em runtime.

### Debugando os binários

```bash
make debug-simple       # qualquer plataforma
make debug-kqueue       # macOS / BSD (usa lldb)
make debug-epoll        # Linux (usa gdb)

# sobrescrever o debugger manualmente
make debug-kqueue DEBUGGER="lldb --"
```

Os alvos `debug-*` dependem de `build`, então o binário é recompilado se necessário antes de entrar no debugger. `BUILD_TYPE` já é `Debug` por padrão, então os símbolos estão presentes sem nenhuma configuração extra.

## Build manual (sem o Makefile)

Se preferir rodar os comandos diretamente:

```bash
# 1. Conan baixa as dependências e gera o toolchain do CMake
#    (cmake_layout posiciona tudo em build/Debug/generators automaticamente)
conan install . --build=missing -s build_type=Debug

# 2. Configura o CMake usando o preset que aponta para o toolchain do Conan
cmake --preset debug

# 3. Builda
cmake --build build/Debug
```

Os binários ficam em `build/Debug/src/<backend>/kernel-io-<backend>`.

### Selecionando backends manualmente

```bash
# desligar o simple e manter só o kqueue (no macOS)
cmake --preset debug -DKIO_BUILD_SIMPLE=OFF -DKIO_BUILD_KQUEUE=ON

# buildar um único alvo
cmake --build build/Debug --target kernel-io-kqueue
```

Forçar um backend incompatível com a plataforma falha cedo:

```bash
cmake --preset debug -DKIO_BUILD_EPOLL=ON
# CMake Error: KIO_BUILD_EPOLL=ON requires Linux (current: Darwin).
```

## Docker (somente backends Linux)

Os backends `simple` e `epoll` rodam em container. **`kqueue` não é containerizável** — Docker só oferece containers Linux, e `kqueue` exige um kernel BSD. Para `kqueue`, rode nativo no host macOS/BSD.

```bash
make docker-epoll       # constrói kernel-io:epoll
make docker-simple      # constrói kernel-io:simple

docker run --rm kernel-io:epoll
docker run --rm kernel-io:simple
```

O `Dockerfile` é multi-stage: o primeiro stage tem `cmake`, `conan` e o toolchain de build, o segundo é um `debian:bookworm-slim` enxuto que recebe só o binário e roda como usuário não-root `kio`.

## Layout do projeto

```
kernel-io/
├── CMakeLists.txt              # raiz: project(), find_package, subdirs
├── CMakePresets.json           # presets debug/release
├── Makefile                    # wrapper conan + cmake
├── conanfile.txt               # spdlog, gtest
├── Dockerfile                  # multi-stage, ARG BACKEND={epoll,simple}
├── cmake/                      # módulos CMake reutilizáveis
│   ├── PreventInSourceBuilds.cmake
│   ├── StandardProjectSettings.cmake
│   ├── ProjectOptions.cmake    # toggles KIO_BUILD_* + guardas de plataforma
│   ├── CompilerWarnings.cmake  # função kio_set_warnings(target)
│   └── StaticAnalysers.cmake   # toggles clang-tidy / cppcheck
├── src/
│   ├── simple/                 # backend select(), portátil
│   ├── epoll/                  # backend Linux
│   └── kqueue/                 # backend macOS/BSD
└── tests/                      # gtest (estrutura pronta, sem testes ativos)
```

## Testes

A infraestrutura GoogleTest já está plumbada via Conan, mas nenhum teste está ativo. Para adicionar:

1. Crie `tests/test_<backend>.cpp` (ex: `tests/test_simple.cpp`).
2. Em [tests/CMakeLists.txt](tests/CMakeLists.txt), descomente a linha correspondente.
3. `make build && ctest --preset debug`.

---

# TCP Network Stack — Fluxo de Pacotes

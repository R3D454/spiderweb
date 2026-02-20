# spiderweb

A brokerless multicast Pub/Sub proof-of-concept written in C++17.

Messages use **16-byte binary UUIDs** for deduplication and support typed
protobuf payloads via `google.protobuf.Any`.  
UDP multicast is used for low-latency broadcast; ZeroMQ over TCP is used as a
fetch fallback when messages are missed (sequence gap recovery).

## Architecture

```
Publisher                       Subscriber(s)
   │                                  │
   │──── UDP multicast (Envelope) ────►│
   │                                  │  (gap detected)
   │◄──── ZMQ REQ (FetchRequest) ─────│
   │───── ZMQ REP (FetchResponse) ───►│
   │                                  │
   │──── UDP multicast (Heartbeat) ──►│  (control channel)
```

### Components

| File | Description |
|------|-------------|
| `proto/transport.proto` | `Envelope`, `Heartbeat`, `FetchRequest`, `FetchResponse` |
| `proto/event.proto` | Example typed payload `example.MyEvent` |
| `src/udp_transport.*` | POSIX multicast send/receive |
| `src/storage.*` | Thread-safe in-memory message store |
| `src/deduplicator.*` | UUID-based duplicate suppression |
| `src/zmq_fetch.*` | ZeroMQ REQ/REP fetch server & client |
| `src/heartbeat.*` | Heartbeat sender helper |
| `src/spiderweb_node.*` | Composed node: publishes, receives, deduplicates, fills gaps |
| `src/main.cpp` | Interactive CLI |

## Prerequisites

- CMake ≥ 3.16
- C++17 compiler (GCC ≥ 8 or Clang ≥ 7)
- Protobuf ≥ 3.x (`libprotobuf-dev`, `protobuf-compiler`)
- ZeroMQ ≥ 4.x (`libzmq3-dev`)
- cppzmq (`libzmqpp-dev` or submodule)
- Optional: libuuid (`uuid-dev`) for UUID generation (random fallback used otherwise)

## Quick start (system libraries)

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install -y cmake libprotobuf-dev protobuf-compiler \
                        libzmq3-dev libcppzmq-dev uuid-dev

git clone https://github.com/R3D454/spiderweb.git
cd spiderweb

mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Quick start (submodules)

```bash
git clone --recurse-submodules https://github.com/R3D454/spiderweb.git
cd spiderweb

# Or, if already cloned:
git submodule update --init --recursive

mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Running two nodes

**Terminal 1 – Node A (publisher)**
```bash
./spiderweb nodeA tcp://*:5555 239.0.0.1 5000 239.0.0.2 5001
```

**Terminal 2 – Node B (subscriber)**
```bash
./spiderweb nodeB tcp://*:5556 239.0.0.1 5000 239.0.0.2 5001
```

**Publish a message from Node A**
```
publish news Hello from spiderweb!
```

**List discovered peers from Node B**
```
peers
```

## Typed protobuf payloads

Use `publishProto<T>` in code to send a typed message:

```cpp
example::MyEvent evt;
evt.set_text("hello");
evt.set_value(42);
node.publishProto("events", evt);
```

The payload is wrapped in `google.protobuf.Any` so subscribers can
`UnpackTo` it once they know the type URL.

## Notes

- **MTU / payload limits**: UDP datagrams are typically limited to ~1500 bytes
  on Ethernet.  For larger payloads use the ZMQ fetch path or fragment manually.
- This is a **proof-of-concept**.  No authentication, encryption, or persistence
  beyond in-process memory is provided.
- No LICENSE file is included; all rights reserved by the author.

## Editor / Language Server

- If your editor or language server (e.g. `clangd`) reported errors from
  headers like `<mmintrin.h>`, ensure the build-generated `compile_commands.json`
  includes the target architecture flags. This project sets SSE options for
  x86 builds in `CMakeLists.txt` so `-msse -msse2` (or `/arch:SSE2` on MSVC)
  are exported to the compilation database when you run `cmake`.
- After regenerating the build files, restart your language server or editor
  so it picks up the updated `compile_commands.json`.

Common ways to restart `clangd`/C++ language servers in editors:

- VS Code (clangd extension): open Command Palette → `Clangd: Restart`.
- VS Code (ms-vscode.cpptools): open Command Palette → `C/C++: Restart Language Server`.
- Or simply close and re-open the workspace/window.

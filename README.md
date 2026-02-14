# NeuralWings Server

Authoritative game server for NeuralWings, powered by **nbnet** with UDP + WebRTC transports.

## Prerequisites

- CMake ≥ 3.11
- C++17 compiler (MSVC / GCC / Clang)
- [vcpkg](https://github.com/microsoft/vcpkg) for `libdatachannel` and `openssl`

## Directory Structure

```
Neural_Wings-server/
├── .vscode/           # VS Code debug & task configs
├── src/               # Server source code
├── shared/            # Protocol headers shared with the client
│   └── Engine/Network/
│       ├── NetTypes.h
│       └── Protocol/
│           ├── MessageTypes.h
│           ├── Messages.h
│           └── PacketSerializer.h
├── third_party/
│   └── nbnet/         # nbnet networking library
├── CMakeLists.txt
├── vcpkg.json         # vcpkg manifest
└── README.md
```

## Build (Windows)

```powershell
# 1. Bootstrap vcpkg (if not already)
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

# 2. Configure & Build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Debug
```

## Build (WSL / Linux)

```bash
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install libdatachannel:x64-linux
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

## Shared Protocol

The `shared/` directory contains protocol headers that **must stay in sync** with the client repository (`Neural_Wings-demo`). If you change message formats, update both sides.

Shared files:

- `Engine/Network/NetTypes.h` — `ClientID`, `NetObjectID`, connection constants
- `Engine/Network/Protocol/MessageTypes.h` — `NetMessageType` enum
- `Engine/Network/Protocol/Messages.h` — POD message structs
- `Engine/Network/Protocol/PacketSerializer.h` — serialize/deserialize helpers

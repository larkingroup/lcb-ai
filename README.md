# lcb-ai

A local AI desktop for Windows and Linux.
Select a llama.cpp server and a GGUF model to start chatting.

## Build

Requires CMake 3.20+ and a C17 compiler.

```console
cmake -S . -B build
cmake --build build --config Release
```

Run `lcb-ai.exe` from `build/Release` or `build`.

Linux also requires C++17, libcurl, OpenSSL, and FLTK 1.4.

```console
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
./build-linux/lcb-ai
```

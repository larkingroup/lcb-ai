# lcb-ai

A local AI desktop for Windows.
Select a llama.cpp server and a GGUF model to start chatting.

## Build

Requires CMake 3.20+ and a C17 compiler.

```console
cmake -S . -B build
cmake --build build --config Release
```

Run `lcb-ai.exe` from `build/Release` or `build`.

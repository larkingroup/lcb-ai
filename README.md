# lti-ai

Local AI processor and interface by Larkin Technical Systems.

Build on Windows with CMake 3.20+ and a C17 compiler, from its developer terminal:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `lti-ai.exe` from `build/Release` (or `build`, depending on the compiler).
Select a local llama.cpp server executable and GGUF model in the app.

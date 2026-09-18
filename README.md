# lcb-ai

Local AI workspace for Windows by Larkin Computing Bureau.

Select a local llama.cpp server and GGUF model in the app. Settings and
conversations are saved locally. Existing LTI/LTS data is supported.

Generation settings are saved per model, including context size, response length,
thinking, and repetition controls. Context changes take effect after reloading
the model. Thinking accepts Auto, Off, or On where supported.

Saved chats have no exchange-count limit. Each request uses the recent exchanges
that fit the model context, with room reserved for the answer. Context usage is
shown in the app. Saved history uses RAM and supports JSON files below 2 GiB.

Requires a compatible llama.cpp server; tested with b10566.

Build with CMake 3.20+ and a C17 compiler:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `lcb-ai.exe` from `build/Release` or `build`, depending on the compiler.

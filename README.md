# lcb-ai

A local AI desktop for Windows and Linux by Larkin Computing Bureau.
Chat with GGUF models through llama.cpp, with saved conversations,
workspaces, and per-model settings.

## Get started

Download a build from [Releases](https://github.com/larkingroup/lcb-ai/releases).
Choose a llama.cpp server and a GGUF model in the app, then load the model
and start chatting. Use a server built for your operating system.

Linux also supports text and code file attachments with any model, and
media attachments when the model and its matching projector support them.
Audio support is experimental.

## Build

Requires CMake 3.20+ and a C17 compiler. Linux also requires C++17, libcurl,
OpenSSL, and FLTK 1.4. CMake downloads FLTK 1.4.5 if it is not installed.

### Windows

```console
cmake -S . -B build
cmake --build build --config Release
```

Run `lcb-ai.exe` from `build/Release` or `build`.

For Windows on ARM64, build with an ARM64 compiler, for example from Linux with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```console
cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_SYSTEM_PROCESSOR=ARM64 -DCMAKE_C_COMPILER=aarch64-w64-mingw32-clang \
  -DCMAKE_RC_COMPILER=aarch64-w64-mingw32-windres -DCMAKE_C_FLAGS=-static
cmake --build build-arm64
```

### Linux

On Fedora, install the build dependencies:

```console
sudo dnf install gcc gcc-c++ cmake ninja-build libcurl-devel openssl-devel \
  libX11-devel libXext-devel libXft-devel libXrender-devel libXfixes-devel \
  libXcursor-devel libXinerama-devel fontconfig-devel freetype-devel \
  wayland-devel wayland-protocols-devel libxkbcommon-devel cairo-devel \
  pango-devel libdecor-devel dbus-devel
```

```console
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
./build-linux/lcb-ai
```

The same steps build on ARM64 (aarch64) Linux.

To package the Linux build:

```console
cd build-linux && cpack
```

This writes a relocatable `.tar.gz` (run `bin/lcb-ai` from the extracted folder),
an `.rpm` when `rpmbuild` is installed, and a `.deb` when `dpkg-deb` is installed,
each with a SHA-256 checksum. Install the RPM with `sudo dnf install ./lcb-ai-*.rpm`.

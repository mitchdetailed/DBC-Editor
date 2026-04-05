# DBC Editor

A general-purpose DBC file editor built with Qt6/C++ for editing CAN database (`.dbc`) files. Designed with CMake for cross-platform compilation.

---

## Features

- Open, edit, create, and save `.dbc` files
- View and manage **Nodes**, **Messages**, **Signals**, **Value Tables**, and **Attributes**
- Visual **bit layout widget** showing signal positions across the CAN frame bytes
- Signal selection and click interaction in the bit layout view
- Import attribute definitions from external files
- Copy/paste support for messages and signals
- Per-message and per-signal comment editing
- Recent file history
- DBC validation (duplicate IDs, names, signal name rules)

---

## Dependencies

| Dependency | Minimum Version | Notes |
|---|---|---|
| [Qt6](https://www.qt.io/download) | 6.x | Core, Gui, Widgets modules required |
| [CMake](https://cmake.org/download/) | 3.16 | Build system |
| C++17 compiler | — | MSVC 2019+, GCC 9+, or Clang 10+ |

> **Windows**: The easiest way to install Qt6 is via the [Qt Online Installer](https://www.qt.io/download-qt-installer). Make sure to select the **MSVC 2019 64-bit** or **MinGW** kit matching your compiler.

---

## Building from Source

### 1. Clone the repository

```bash
git clone https://github.com/your-username/DBC-Editor.git
cd DBC-Editor
```

### 2. Configure with CMake

Replace `C:/Qt/6.x.x/msvc2019_64` with the actual path to your Qt6 installation.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2019_64"
```

### 3. Build

```bash
cmake --build build --config Release
```

The compiled executable will be placed in `build/Release/QtDbcEditor.exe`.

### 4. Deploy Qt dependencies (Windows)

After building, run `windeployqt` to copy the required Qt DLLs alongside the executable:

```bash
"C:/Qt/6.7.2/msvc2019_64/bin/windeployqt.exe" build/Release/QtDbcEditor.exe
```

The `build/Release/` folder will then be self-contained and can be distributed or run without a Qt install.

---

## Precompiled Binaries

Precompiled binaries for all platforms are available on the [Releases](../../releases) page — no build tools or Qt installation required.

| File | Platform |
|---|---|
| `QtDbcEditor-vX.X.X-windows-x64.zip` | Windows 10/11 (x64) — extract and run `QtDbcEditor.exe` |
| `QtDbcEditor-vX.X.X-windows-arm64.zip` | Windows 11 (ARM64, e.g. Surface Pro X, Snapdragon) |
| `QtDbcEditor-vX.X.X-linux-x86_64.AppImage` | Ubuntu 22.04+ / most Linux distros — `chmod +x` then run |
| `QtDbcEditor-vX.X.X-macos-arm64.dmg` | macOS (Apple Silicon — M1/M2/M3/M4) |
| `QtDbcEditor-vX.X.X-macos-x64.dmg` | macOS (Intel) |

---

## License

See [LICENSE](LICENSE) for details.

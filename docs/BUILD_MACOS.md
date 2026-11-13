# Building for macOS (.app Bundles)

This guide provides instructions for compiling Kotonoha into standalone macOS application bundles (`.app`). You can build either **via CMake (Command Line / Ninja)** or **via Xcode**.

## Prerequisites

- **macOS** with Xcode Command Line Tools / Xcode installed.
- **CMake**, **pkg-config**, and dependencies (`ffmpeg`, `json-c`, `libass`, `freetype`, `harfbuzz`, `plutosvg`, `SDL3`).

---

## Method 1: Building via CMake (Command Line / Ninja)

This method is ideal for command-line workflows and automated builds.

### 1. Configure with macOS Bundles Enabled
Configure the project specifying `-G Ninja` and `-DKOTONOHA_BUILD_MACOS_BUNDLES=ON`:

```bash
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
```

### 2. Build the Game Target
Build your target of choice (e.g., `RealLive` or `Amagami`):

```bash
cmake --build build/macos --target RealLive
```

---

## Method 2: Building via Xcode Generator

You can generate a native Xcode project using CMake's Xcode generator to build and debug within the Xcode IDE.

### 1. Configure with Xcode Generator
Configure the project with `-G Xcode`:

```bash
cmake -S . -B build/macos-xcode -G Xcode \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
```

### 2. Open and Build in Xcode
1. Open the generated project in Xcode:
   ```bash
   open build/macos-xcode/Kotonoha.xcodeproj
   ```
2. Select your target (e.g., `RealLive`) and build/run (`Cmd + R`).

---

## Output

The compiled `.app` bundle will be generated at:
`build/outputs/RealLive/RealLive.app` (or within the Xcode build product directory).
All shared dylibs and assets (`assets/` and `game-assets/`) are automatically relocated into `Contents/Frameworks` and `Contents/Resources`.

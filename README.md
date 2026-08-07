# Phoenix CGAL App

This repository contains a small cross-platform C++ application that uses the
[CGAL](https://www.cgal.org/) library and is designed to be developed on
Windows and compiled on Linux as well.

## Goals

- Use a standard CMake-based build.
- Keep the code portable across Windows and Linux.
- Avoid IDE-specific project files.
- Make dependency resolution explicit and reproducible.

## Project Layout

```text
.
|-- CMakeLists.txt
|-- .gitignore
|-- CMakePresets.json
|-- scripts
|   |-- build-linux.sh
|   `-- build-windows.ps1
`-- src
    `-- main.cpp
```

## Dependencies

This project expects CGAL to be installed on the target system, along with its
required dependencies.

Typical CGAL dependencies include:

- Boost
- GMP
- MPFR

Exact installation details depend on how you install CGAL.

## Build On Windows

The most practical approach on Windows is usually `vcpkg`.

### 1. Bootstrap vcpkg and set `VCPKG_ROOT`

Example:

```powershell
$env:VCPKG_ROOT="C:\dev\vcpkg"
```

### 2. Configure with the preset

```powershell
cmake --preset windows-default
```

### 3. Build

```powershell
cmake --build --preset build-windows-release
```

### 4. Or use the script

```powershell
.\scripts\build-windows.ps1
.\scripts\build-windows.ps1 -Configuration Debug
```

#### Windows build script

Script: `scripts/build-windows.ps1`

Purpose:

- verifies that `cmake` is available on `PATH`
- verifies that `VCPKG_ROOT` is set
- runs `cmake --preset windows-default`
- builds either the `Debug` or `Release` preset

Parameters:

- `-Configuration Release`
- `-Configuration Debug`

Requirements:

- CMake installed on Windows
- a C++ compiler supported by your CMake generator
- `vcpkg` installed
- `VCPKG_ROOT` pointing to your local `vcpkg` checkout

Example:

```powershell
$env:VCPKG_ROOT="C:\dev\vcpkg"
.\scripts\build-windows.ps1 -Configuration Release
```

## Build On Linux

On Debian/Ubuntu-like systems you can either use system packages or `vcpkg`.

### Option A: system packages

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcgal-dev
cmake -S . -B build
cmake --build build -j
```

### Option B: vcpkg

If you also want Linux builds to use the same dependency manager as Windows:

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset linux-default
cmake --build --preset build-linux-release
```

Or use the script:

```bash
./scripts/build-linux.sh
./scripts/build-linux.sh Debug
```

#### Linux build script

Script: `scripts/build-linux.sh`

Purpose:

- verifies that `cmake` is available on `PATH`
- configures the project with `linux-default`
- builds either the `Debug` or `Release` preset

Arguments:

- `Release`
- `Debug`

Requirements:

- CMake installed on Linux
- a working C++ compiler toolchain
- CGAL and dependencies available either from system packages or `vcpkg`
- if using the provided preset, `VCPKG_ROOT` should be set

Example:

```bash
export VCPKG_ROOT=$HOME/vcpkg
chmod +x ./scripts/build-linux.sh
./scripts/build-linux.sh Release
```

## Build Script Reference

### scripts/build-windows.ps1

Usage:

```powershell
.\scripts\build-windows.ps1
.\scripts\build-windows.ps1 -Configuration Debug
```

Behavior:

- default configuration is `Release`
- fails immediately if `cmake` is missing
- fails immediately if `VCPKG_ROOT` is not set
- configures into the preset build directory under `build/windows-default`

### scripts/build-linux.sh

Usage:

```bash
./scripts/build-linux.sh
./scripts/build-linux.sh Debug
```

Behavior:

- default configuration is `Release`
- accepts only `Debug` or `Release`
- fails immediately if `cmake` is missing
- configures into the preset build directory under `build/linux-default`

### Output locations

The scripts configure CMake into preset-specific folders:

- `build/windows-default`
- `build/linux-default`

The final executable location depends on the generator and configuration. With
the current presets, expect the executable under the corresponding build folder
for that platform and configuration.

## Notes On Portability

- The project uses `CMake` and `find_package(CGAL REQUIRED)`.
- A `vcpkg.json` manifest is included for consistent dependency setup.
- Build helper scripts are included for both Windows and Linux.
- Source code avoids Windows-specific APIs.
- Compiler settings are applied through CMake rather than IDE files.
- The sample code uses CGAL kernel types and should compile on both MSVC and
  GCC/Clang.

## Run

After building, run the executable from the build output directory:

```bash
./build/phoenix_cgal_app
```

On Windows with multi-config generators, the executable is typically under a
configuration directory such as:

```powershell
.\build\Release\phoenix_cgal_app.exe
```

## Next Steps

Good follow-up improvements could be:

- add tests
- pin dependencies with a package manager manifest
- expand the CGAL example into your real geometry workflow

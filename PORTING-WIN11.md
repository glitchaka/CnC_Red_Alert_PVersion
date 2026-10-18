# Native Windows 11 port

This fork is being ported so that its own Red Alert source tree builds with a modern Windows toolchain. The previous GitHub Actions workflow that checked out and compiled TheAssemblyArmada/Vanilla-Conquer is removed because it did not build this repository.

## Target

- Windows 11 x64
- Visual Studio 2022 / MSVC
- CMake
- No external engine checkout
- Replace or isolate Watcom/TASM, DirectX 5, Greenleaf GCL and HMI SOS dependencies as required

## Current porting order

1. Inventory the legacy makefiles and platform abstraction points.
2. Establish a native CMake target over this repository's source files.
3. Replace compiler-specific headers, pragmas and assembly boundaries.
4. Replace obsolete graphics/audio/network dependencies with maintained equivalents.
5. Compile the game executable from this fork and fix errors iteratively.

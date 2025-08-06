# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

slang-rhi is a render hardware interface library for the Slang shading language. It provides abstraction layers for multiple graphics APIs including D3D11, D3D12, Vulkan, Metal, CUDA, WebGPU, and a CPU backend.

## Build Instructions

This project uses CMake with presets for different configurations:

```bash
# Configure with default preset (Ninja Multi-Config)
cmake --preset default

# Configure with specific compiler presets
cmake --preset clang    # Clang compiler
cmake --preset gcc      # GCC compiler
cmake --preset msvc     # MSVC on Windows

# Build specific configuration
cmake --build build --config Debug
cmake --build build --config Release
cmake --build build --config RelWithDebInfo
```

## Running Tests

Tests are built when `SLANG_RHI_BUILD_TESTS` is ON (default for the master project):

```bash
# Run tests from build directory
./build/Debug/slang-rhi-tests
./build/Release/slang-rhi-tests

# Run with device check
./build/Debug/slang-rhi-tests -check-devices
```

## Code Architecture

### Core Structure

- **include/**: Public API headers
  - `slang-rhi.h`: Main public API
  - `slang-rhi/*.h`: Additional public headers

- **src/**: Implementation files organized by backend
  - `core/`: Common utilities (allocators, smart pointers, platform abstractions)
  - `cpu/`: CPU backend implementation
  - `cuda/`: CUDA backend
  - `d3d11/`, `d3d12/`: Direct3D backends
  - `vulkan/`: Vulkan backend
  - `metal/`: Metal backend (macOS/iOS)
  - `wgpu/`: WebGPU backend
  - `debug-layer/`: Debug validation layer

### Key Concepts

1. **Device Abstraction**: Each backend implements the `IDevice` interface which creates resources and pipelines
2. **Command Recording**: Commands are recorded into command buffers/encoders for execution
3. **Shader Objects**: Abstraction for shader parameters and bindings compatible with Slang
4. **Resource Management**: Unified handling of buffers, textures, and acceleration structures

### Backend Selection

Backends are conditionally compiled based on platform:
- Windows: D3D11, D3D12, Vulkan, CUDA, WebGPU
- Linux: Vulkan, CUDA, WebGPU
- macOS: Metal, Vulkan, WebGPU

## Development Guidelines

- Use existing backend implementations as reference when adding new features
- Follow the established naming conventions (e.g., `DeviceImpl`, `BufferImpl` for backend implementations)
- Tests should cover all enabled backends using the device capabilities system
- Shader files use the `.slang` extension and are compiled at runtime via the Slang compiler

## Common Tasks

### Adding a new API method
1. Add the method to the appropriate interface in `include/slang-rhi.h`
2. Implement in each backend under `src/<backend>/`
3. Add to the debug layer in `src/debug-layer/`
4. Update `docs/api.md` with implementation status
5. Add tests in `tests/`

### Running a single test
```bash
./build/Debug/slang-rhi-tests -tc="TestName"
```

### Debugging shader compilation
Set the `SLANG_RHI_DEBUG` preprocessor flag or use Debug builds to enable additional logging.

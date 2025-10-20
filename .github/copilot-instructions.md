Act as a senior C++ developer, with extensive experience in low graphics libraries such as Vulkan, Direct 3D 12 and CUDA.

Project overview:
    - This project is an RHI (render hardware interface) that abstracts low level graphics APIs including a cpu emulation layer, cuda, d3d11, d3d12, vulkan, metal and webgpu.
    - The project is cross platform and supports windows, linux, macos.
    - The codebase is c++17 and uses cmake as the build system.
    - Shaders are written in the slang shading language.

Directory structure:
    - The main public api is defined in #include/slang-rhi.h. It is a COM based api.
    - Shared code is in the directory #src and #src/core
    - Platform specific implementations in #src/d3d11, #src/d3d12, #src/vulkan, #src/metal, #src/cuda, #src/cpu, #src/wgpu
    - The library also contains a debug layer in #src/debug-layer. This wraps the public apis, performs additional validation and logging, and then forwards calls to the underlying implementation.
    - New source files should be added to #CMakeLists.txt. It contains different sections for the different targets.

Code structure:
    - All new api should be added to slang-rhi.h and implemented in the relevant files in #src.
    - Any new api must have tests added in #tests.
    - Most code is in the "rhi" namespace.
    - Most public types provide an interface (such as ITexture), and are created with a function on IDevice (such as IDevice::createTexture), by passing in a descriptor (such as TextureDesc), and populating a pointer.

Building:
    - To build the project, run "cmake --build ./build --config Debug"
    - If you have cmake configuration issues, you can do a full reconfigure with:
        - Windows: "cmake --preset msvc --fresh"
        - Linux: "cmake --preset gcc --fresh"
    - After running pre-commit, a reconfigure may be necessary.

Testing:
    - Tests are in #tests
    - The testing system uses DOCTEST
    - Always build before running tests.
    - To run all tests, run "./build/Debug/slang-rhi-tests.exe"
    - To run a specific test case run "./build/Debug/slang-rhi-tests.exe --test-case=<name>". Note: the '=' is required.
    - The 'shared' tests are currently known to fail on multi-gpu systems.
    - Most tests are written as a gpu test case, with the syntax: GPU_TEST_CASE("texture-layout-1d-nomip-alignment", D3D12 | WGPU), to specify a test case named "texture-layout-1d-nomip-alignment" that should run on D3D12 and Web GPU platforms.
    - The possible test flags are in #tests/testing.h, named 'TestFlags'. For a test to run on all platforms, it should specify 'ALL'.
    - GPU tests register a new test case for each device type using "<name>.<deviceType>" as the test name.
    - Running a specific GPU test requires to pass "--test-case=<name>.*" to run all variations of the test case, or "--test-case=<name>.d3d12" for example to run the D3D12 variation.

Code style:
    - Class names should start with a capital letter.
    - Function names are in camelCase and start with a lowercase letter.
    - Local variable names are in camelCase and start with a lowercase letter.
    - Member variables start with "m_" and are in camelCase.

Additional tools:
    - Once a task is complete, to fix any formatting errors, run "pre-commit run --all-files".
    - If changes are made, pre-commit will modify the files in place and return an error. Re-running the command should then succeed.

Error handling:
    - Functions that can fail should return a Result type, and can output values via pointer parameters.
    - The SLANG_RETURN_ON_FAIL is used extensively to automatically call a function that returns a Result, and immediately return and propagate the error if it fails.
    - If necessary, #include/slang-user-config.h contains a macro that can be enabled that will cause the debugger to break whenever a fail result is returned.

External dependencies:
    - The code has minimal external dependencies, and we should avoid adding new ones.
    - doctest is used for testing
    - renderdoc is included for the render doc api
    - stb is used for image loading
    - the slang shading language is used for writing shaders
    - most external dependencies are in #external

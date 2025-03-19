#include "cuda-nvrtc.h"
#include "core/platform.h"
#include "core/deferred.h"

#include <filesystem>

#include <iostream> // TODO remove

#if SLANG_WINDOWS_FAMILY
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winreg.h>
#elif SLNAG_LINUX_FAMILY
#include <cstdlib>
#endif

namespace rhi::cuda {

struct NVRTC::Impl
{
    std::filesystem::path cudaPath;
    std::filesystem::path cudaIncludePath;
    std::filesystem::path nvrtcLibraryPath;
    SharedLibraryHandle nvrtcLibrary = nullptr;
};

#if SLANG_WINDOWS_FAMILY
inline void findCUDAPathFromEnvironment(std::vector<std::filesystem::path>& outPaths)
{
    char cudaPath[MAX_PATH];
    if (GetEnvironmentVariableA("CUDA_PATH", cudaPath, MAX_PATH))
    {
        outPaths.push_back(std::filesystem::path(cudaPath));
    }
}
inline void findCUDAPathFromDefaultPaths(std::vector<std::filesystem::path>& outPaths)
{
    std::filesystem::path defaultPath{"C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA"};
    auto it = std::filesystem::directory_iterator(defaultPath);
    for (const auto& entry : it)
    {
        if (entry.is_directory())
        {
            outPaths.push_back(defaultPath / entry.path());
        }
    }
}
#elif SLANG_LINUX_FAMILY
inline void findCUDAPathFromEnvironment(std::vector<std::filesystem::path>& outPaths)
{
    const char* cudaPath = getenv("CUDA_PATH");
    if (cudaPath)
    {
        outPaths.push_back(std::filesystem::path(cudaPath));
    }
}
inline void findCUDAPathFromDefaultPaths(std::vector<std::filesystem::path>& outPaths)
{
    std::filesystem::path defaultPath{"/usr/local"};
    auto it = std::filesystem::directory_iterator(defaultPath);
    for (const auto& entry : it)
    {
        if (entry.is_directory() && entry.path().filename().string().substr(0, 4) == "cuda")
        {
            outPaths.push_back(entry.path());
        }
    }
}
#else
#error "Unsupported platform"
#endif

inline bool findNVRTCLibrary(
    const std::filesystem::path& cudaPath,
    std::string_view libPath,
    std::string_view prefix,
    std::string_view extension,
    std::filesystem::path& outPath
)
{
    std::filesystem::directory_iterator it(cudaPath / libPath);
    for (const auto& entry : it)
    {
        if (entry.is_regular_file())
        {
            if (entry.path().filename().string().substr(0, prefix.size()) == prefix &&
                entry.path().extension().string() == extension)
            {
                outPath = cudaPath / libPath / entry.path();
                return true;
            }
        }
    }
    return false;
}

NVRTC::NVRTC()
{
    m_impl = new Impl();
}

NVRTC::~NVRTC()
{
    if (m_impl->nvrtcLibrary)
    {
        unloadSharedLibrary(m_impl->nvrtcLibrary);
    }

    delete m_impl;
}

Result NVRTC::init()
{
    std::vector<std::filesystem::path> cudaPaths;
    findCUDAPathFromEnvironment(cudaPaths);
    findCUDAPathFromDefaultPaths(cudaPaths);

    for (const auto& cudaPath : cudaPaths)
    {
#if SLANG_WINDOWS_FAMILY
        if (findNVRTCLibrary(cudaPath, "bin", "nvrtc64_", ".dll", m_impl->nvrtcLibraryPath))
#elif SLANG_LINUX_FAMILY
        if (findNVRTCLibrary(cudaPath, "lib64", "libnvrtc", ".so", m_impl->nvrtcLibraryPath))
#endif
        {
            m_impl->cudaPath = cudaPath;
            break;
        }
    }

    if (!std::filesystem::exists(m_impl->nvrtcLibraryPath))
    {
        return SLANG_FAIL;
    }

    m_impl->cudaIncludePath = m_impl->cudaPath / "include";

    SLANG_RETURN_ON_FAIL(loadSharedLibrary(m_impl->nvrtcLibraryPath.string().c_str(), m_impl->nvrtcLibrary));

    // clang-format off
    nvrtcGetErrorString = (nvrtcGetErrorStringFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcGetErrorString");
    nvrtcVersion = (nvrtcVersionFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcVersion");
    nvrtcCreateProgram = (nvrtcCreateProgramFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcCreateProgram");
    nvrtcDestroyProgram = (nvrtcDestroyProgramFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcDestroyProgram");
    nvrtcCompileProgram = (nvrtcCompileProgramFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcCompileProgram");
    nvrtcGetPTXSize = (nvrtcGetPTXSizeFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcGetPTXSize");
    nvrtcGetPTX = (nvrtcGetPTXFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcGetPTX");
    nvrtcGetProgramLogSize = (nvrtcGetProgramLogSizeFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcGetProgramLogSize");
    nvrtcGetProgramLog = (nvrtcGetProgramLogFunc*)findSymbolAddressByName(m_impl->nvrtcLibrary, "nvrtcGetProgramLog");
    // clang-format on

    if (!nvrtcGetErrorString || !nvrtcVersion || !nvrtcCreateProgram || !nvrtcDestroyProgram || !nvrtcCompileProgram ||
        !nvrtcGetPTXSize || !nvrtcGetPTX || !nvrtcGetProgramLogSize || !nvrtcGetProgramLog)
    {
        return SLANG_FAIL;
    }

    return SLANG_OK;
}

Result NVRTC::compilePTX(const char* source, CompileResult& outResult)
{
    nvrtcProgram prog;
    outResult.result = nvrtcCreateProgram(&prog, source, "dummy.cu", 0, nullptr, nullptr);
    if (outResult.result != NVRTC_SUCCESS)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ nvrtcDestroyProgram(&prog); });

    std::string includePath = m_impl->cudaIncludePath.string();
    const char* options[] = {"-I", includePath.c_str()};

    // Compile the CUDA program
    outResult.result = nvrtcCompileProgram(prog, SLANG_COUNT_OF(options), options);

    // Retrieve log
    size_t logSize;
    if (nvrtcGetProgramLogSize(prog, &logSize) == NVRTC_SUCCESS)
    {
        outResult.log.resize(logSize);
        if (nvrtcGetProgramLog(prog, outResult.log.data()) != NVRTC_SUCCESS)
        {
            outResult.log.clear();
        }
    }

    // Early out if compilation failed
    if (outResult.result != NVRTC_SUCCESS)
    {
        return SLANG_FAIL;
    }

    // Retrieve PTX
    size_t ptxSize;
    outResult.result = nvrtcGetPTXSize(prog, &ptxSize);
    if (outResult.result != NVRTC_SUCCESS)
    {
        return SLANG_FAIL;
    }
    outResult.ptx.resize(ptxSize);
    outResult.result = nvrtcGetPTX(prog, outResult.ptx.data());
    return outResult.result == NVRTC_SUCCESS ? SLANG_OK : SLANG_FAIL;
}

} // namespace rhi::cuda

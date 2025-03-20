#include "cuda-nvrtc.h"
#include "core/platform.h"
#include "core/deferred.h"

#include <filesystem>

#if SLANG_WINDOWS_FAMILY
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winreg.h>
#elif SLANG_LINUX_FAMILY
#include <cstdlib>
#endif

namespace rhi::cuda {

struct NVRTC::Impl
{
    SharedLibraryHandle nvrtcLibrary = nullptr;
    std::filesystem::path nvrtcPath;
    std::filesystem::path cudaIncludePath;
};

#if SLANG_WINDOWS_FAMILY
inline void findNVRTCPaths(std::vector<std::filesystem::path>& outPaths)
{
    // First, check for "CUDA_PATH" environment variable.
    {
        char path[MAX_PATH];
        if (GetEnvironmentVariableA("CUDA_PATH", path, MAX_PATH))
        {
            outPaths.push_back(std::filesystem::path(path) / "bin");
        }
    }
    // Next, check default installation paths.
    {
        std::filesystem::path defaultPath{"C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA"};
        std::vector<std::filesystem::path> versions;
        auto it = std::filesystem::directory_iterator(defaultPath);
        for (const auto& entry : it)
        {
            if (entry.is_directory())
            {
                versions.push_back(entry.path());
            }
        }

        std::sort(versions.begin(), versions.end(), std::greater<>());
        for (const auto& version : versions)
        {
            outPaths.push_back(version / "bin");
        }
    }
}
#elif SLANG_LINUX_FAMILY
inline void findNVRTCPaths(std::vector<std::filesystem::path>& outPaths)
{
    // First, check for "CUDA_PATH" environment variable.
    {
        const char* path = getenv("CUDA_PATH");
        if (path)
        {
            outPaths.push_back(std::filesystem::path(path) / "lib64");
        }
    }
    // Next, check default installation paths.
    {
        std::filesystem::path defaultPath{"/usr/local"};
        auto it = std::filesystem::directory_iterator(defaultPath);
        for (const auto& entry : it)
        {
            if (entry.is_directory() && entry.path().filename().string().substr(0, 4) == "cuda")
            {
                outPaths.push_back(entry.path() / "lib64");
            }
        }
    }
    // Finally, check common system paths.
    outPaths.push_back("/usr/lib/x86_64-linux-gnu/");
}
#else
#error "Unsupported platform"
#endif

inline bool findNVRTCLibrary(
    const std::filesystem::path& basePath,
    std::string_view prefix,
    std::string_view extension,
    std::filesystem::path& outPath
)
{
    if (!std::filesystem::exists(basePath))
    {
        return false;
    }
    std::filesystem::directory_iterator it(basePath);
    for (const auto& entry : it)
    {
        if (entry.is_regular_file())
        {
            if (entry.path().stem().string().substr(0, prefix.size()) == prefix &&
                entry.path().stem().string().find('.') == std::string::npos &&
                entry.path().extension().string() == extension)
            {
                outPath = basePath / entry.path();
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
    // Try to find & load NVRTC library.
    {
        std::vector<std::filesystem::path> candidatePaths;
        findNVRTCPaths(candidatePaths);
        for (const auto& path : candidatePaths)
        {
            std::filesystem::path nvrtcPath;
#if SLANG_WINDOWS_FAMILY
            if (findNVRTCLibrary(path, "nvrtc64_", ".dll", nvrtcPath))
#elif SLANG_LINUX_FAMILY
            if (findNVRTCLibrary(path, "libnvrtc", ".so", nvrtcPath))
#else
#error "Unsupported platform"
#endif
            {
                if (loadSharedLibrary(nvrtcPath.string().c_str(), m_impl->nvrtcLibrary) == SLANG_OK)
                {
                    m_impl->nvrtcPath = nvrtcPath;
                    break;
                }
            }
        }
    }

    // Return failure if NVRTC library was not found.
    if (!m_impl->nvrtcLibrary)
    {
        return SLANG_FAIL;
    }

    // Load NVRTC functions.
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

    // Find CUDA include path (containing cuda_runtime.h)
    std::vector<std::filesystem::path> candidatePaths;
    candidatePaths.push_back(m_impl->nvrtcPath.parent_path().parent_path() / "include");
#if SLANG_LINUX_FAMILY
    candidatePaths.push_back("/usr/include");
#endif
    for (const auto& path : candidatePaths)
    {
        if (std::filesystem::exists(path / "cuda_runtime.h"))
        {
            m_impl->cudaIncludePath = path;
            break;
        }
    }
    if (m_impl->cudaIncludePath.empty())
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

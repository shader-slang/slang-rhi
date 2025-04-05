#include "wgpu-api.h"

#include <filesystem>

namespace rhi::wgpu {

API::~API()
{
    if (m_module)
    {
        unloadSharedLibrary(m_module);
    }
}

Result API::init()
{
#if SLANG_WINDOWS_FAMILY
    const char* libraryNames[] = {"dawn.dll", "webgpu_dawn.dll"};
#elif SLANG_LINUX_FAMILY
    const char* libraryNames[] = {"libdawn.so"};
#elif SLANG_APPLE_FAMILY
    const char* libraryNames[] = {"libdawn.dylib"};
#else
    const char* libraryNames[] = {};
#endif

    // We expect dawn to be in the same directory as the slang-rhi library (or the executable/library linking to it).
    const char* rhiPathStr = findSharedLibraryPath((void*)&getRHI);
    if (!rhiPathStr)
    {
        return SLANG_FAIL;
    }
    std::filesystem::path rhiPath = std::filesystem::path(rhiPathStr).parent_path();

    for (const char* name : libraryNames)
    {
        std::filesystem::path path = rhiPath / name;
        if (loadSharedLibrary(path.string().c_str(), m_module) == SLANG_OK)
            break;
    }
    if (!m_module)
        return SLANG_FAIL;

#define LOAD_PROC(name) wgpu##name = (WGPUProc##name)findSymbolAddressByName(m_module, "wgpu" #name);
    SLANG_RHI_WGPU_PROCS(LOAD_PROC)
#undef LOAD_PROC
    return SLANG_OK;
}

} // namespace rhi::wgpu

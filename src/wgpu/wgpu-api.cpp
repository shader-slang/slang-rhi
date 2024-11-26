#include "wgpu-api.h"

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
    char const* libraryNames[] = {"dawn.dll", "webgpu_dawn.dll"};
#elif SLANG_LINUX_FAMILY
    char const* libraryNames[] = {"libdawn.so"};
#elif SLANG_APPLE_FAMILY
    char const* libraryNames[] = {"libdawn.dylib"};
#else
    char const* libraryNames[] = {};
#endif

    for (char const* name : libraryNames)
    {
        if (loadSharedLibrary(name, m_module) == SLANG_OK)
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

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
    SLANG_RETURN_ON_FAIL(loadSharedLibrary("dawn.dll", m_module));
#elif SLANG_LINUX_FAMILY
    SLANG_RETURN_ON_FAIL(loadSharedLibrary("libdawn.so", m_module));
#elif SLANG_APPLE_FAMILY
    SLANG_RETURN_ON_FAIL(loadSharedLibrary("libdawn.dylib", m_module));
#else
    return SLANG_FAIL;
#endif

#define LOAD_PROC(name) wgpu##name = (WGPUProc##name)findSymbolAddressByName(m_module, "wgpu" #name);
    SLANG_RHI_WGPU_PROCS(LOAD_PROC)
#undef LOAD_PROC
    return SLANG_OK;
}

} // namespace rhi::wgpu

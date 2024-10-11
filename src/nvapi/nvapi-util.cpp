#include "nvapi-util.h"
#include "nvapi-include.h"

namespace rhi {

static Result g_initStatus = SLANG_E_UNINITIALIZED;

Result NVAPIUtil::initialize()
{
#if SLANG_RHI_ENABLE_NVAPI
    if (g_initStatus == SLANG_E_UNINITIALIZED)
    {
        NvAPI_Status ret = NVAPI_OK;
        ret = NvAPI_Initialize();
        g_initStatus = (ret == NVAPI_OK) ? SLANG_OK : SLANG_E_NOT_AVAILABLE;
    }
#else
    g_initStatus = SLANG_E_NOT_AVAILABLE;
#endif

    return g_initStatus;
}

bool NVAPIUtil::isAvailable()
{
    return SLANG_SUCCEEDED(g_initStatus);
}

} // namespace rhi

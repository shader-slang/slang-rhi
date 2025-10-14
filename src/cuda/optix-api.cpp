#include "optix-api.h"

#include "cuda-device.h"

namespace rhi::cuda::optix {

#if SLANG_RHI_ENABLE_OPTIX

#define IMPORT_OPTIX_API(tag)                                                                                          \
    namespace tag {                                                                                                    \
    extern uint32_t optixVersion;                                                                                      \
    bool initialize(IDebugCallback* debugCallback);                                                                    \
    Result createContext(const ContextDesc& desc, Context** outContext);                                               \
    }

IMPORT_OPTIX_API(v8_0)
IMPORT_OPTIX_API(v8_1)
IMPORT_OPTIX_API(v9_0)

struct OptixAPI
{
    uint32_t optixVersion;
    bool (*initialize)(IDebugCallback* /*debugCallback*/);
    Result (*createContext)(const ContextDesc& /*desc*/, Context** /*outContext*/);
};

static OptixAPI s_optixAPIs[] = {
    {v9_0::optixVersion, v9_0::initialize, v9_0::createContext},
    // Disable older versions for now, as Slang doesn't properly support generating code for them.
    // {v8_1::optixVersion, v8_1::initialize, v8_1::createContext},
    // {v8_0::optixVersion, v8_0::initialize, v8_0::createContext},
};

Result createContext(const ContextDesc& desc, Context** outContext)
{
    for (auto& api : s_optixAPIs)
    {
        if (desc.requiredOptixVersion != 0 && desc.requiredOptixVersion != api.optixVersion)
        {
            continue;
        }
        if (api.initialize(desc.device->m_debugCallback))
        {
            Result result = api.createContext(desc, outContext);
            if (SLANG_SUCCEEDED(result) || desc.requiredOptixVersion != 0)
            {
                return result;
            }
        }
    }
    return SLANG_E_NOT_AVAILABLE;
}

#else // SLANG_RHI_ENABLE_OPTIX

Result createContext(const ContextDesc& desc, Context** outContext)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outContext);
    return SLANG_E_NOT_AVAILABLE;
}

#endif // SLANG_RHI_ENABLE_OPTIX

} // namespace rhi::cuda::optix

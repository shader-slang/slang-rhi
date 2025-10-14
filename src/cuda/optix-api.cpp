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

// The SLANG_RHI_OPTIX_VERSIONS_X macro is generated in CMakeLists.txt
// It expands to a list of invocations of the given macro, one for each enabled OptiX version (in descending order).
SLANG_RHI_OPTIX_VERSIONS_X(IMPORT_OPTIX_API)

struct OptixAPI
{
    uint32_t optixVersion;
    bool (*initialize)(IDebugCallback* /*debugCallback*/);
    Result (*createContext)(const ContextDesc& /*desc*/, Context** /*outContext*/);
};

#define DEFINE_OPTIX_API(tag) {tag::optixVersion, tag::initialize, tag::createContext},

static OptixAPI s_optixAPIs[] = {SLANG_RHI_OPTIX_VERSIONS_X(DEFINE_OPTIX_API)};

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

#include "optix-api.h"

#include "cuda-device.h"

#if SLANG_RHI_ENABLE_OPTIX

#define IMPORT_OPTIX_API(tag)                                                                                          \
    namespace rhi::cuda::optix::tag {                                                                                  \
    extern uint32_t optixVersion;                                                                                      \
    bool initialize(IDebugCallback* debugCallback);                                                                    \
    Result createContext(const ContextDesc& desc, Context** outContext);                                               \
    }                                                                                                                  \
    namespace rhi::optix_denoiser::tag {                                                                               \
    Result createOptixDenoiserAPI(::rhi::optix_denoiser::IOptixDenoiserAPI** outAPI);                                  \
    }

// The SLANG_RHI_OPTIX_VERSIONS_X macro is generated in CMakeLists.txt
// It expands to a list of invocations of the given macro, one for each enabled OptiX version (in descending order).
SLANG_RHI_OPTIX_VERSIONS_X(IMPORT_OPTIX_API)

struct OptixAPI
{
    uint32_t optixVersion;
    bool (*initialize)(rhi::IDebugCallback* /*debugCallback*/);
    rhi::Result (*createContext)(
        const rhi::cuda::optix::ContextDesc& /*desc*/,
        rhi::cuda::optix::Context** /*outContext*/
    );
    rhi::Result (*createOptixDenoiserAPI)(rhi::optix_denoiser::IOptixDenoiserAPI** /*outAPI*/);
};

#define DEFINE_OPTIX_API(tag)                                                                                          \
    {                                                                                                                  \
        rhi::cuda::optix::tag::optixVersion,                                                                           \
        rhi::cuda::optix::tag::initialize,                                                                             \
        rhi::cuda::optix::tag::createContext,                                                                          \
        rhi::optix_denoiser::tag::createOptixDenoiserAPI,                                                              \
    },

static OptixAPI s_optixAPIs[] = {SLANG_RHI_OPTIX_VERSIONS_X(DEFINE_OPTIX_API)};

#endif // SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda::optix {

#if SLANG_RHI_ENABLE_OPTIX

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

// Denoiser API

#if SLANG_RHI_ENABLE_OPTIX

extern "C" SLANG_RHI_API rhi::Result SLANG_STDCALL
rhiCreateOptixDenoiserAPI(uint32_t optixVersion, rhi::optix_denoiser::IOptixDenoiserAPI** outAPI)
{
    for (auto& api : s_optixAPIs)
    {
        if (optixVersion != 0 && optixVersion != api.optixVersion)
        {
            continue;
        }
        if (api.initialize(nullptr))
        {
            rhi::Result result = api.createOptixDenoiserAPI(outAPI);
            if (SLANG_SUCCEEDED(result) || optixVersion != 0)
            {
                return result;
            }
        }
    }
    return SLANG_E_NOT_AVAILABLE;
}

#else // SLANG_RHI_ENABLE_OPTIX

extern "C" SLANG_RHI_API rhi::Result SLANG_STDCALL
rhiCreateOptixDenoiserAPI(uint32_t optixVersion, rhi::optix_denoiser::IOptixDenoiserAPI** outAPI)
{
    SLANG_UNUSED(optixVersion);
    SLANG_UNUSED(outAPI);
    return SLANG_E_NOT_AVAILABLE;
}

#endif // SLANG_RHI_ENABLE_OPTIX

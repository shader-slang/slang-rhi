#include "metal-shader-table.h"
#include "metal-device.h"

namespace rhi::metal {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(RayTracingPipeline* pipeline)
{
    return RefPtr<Buffer>(0);
}

} // namespace rhi::metal

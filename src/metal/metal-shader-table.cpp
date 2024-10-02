#include "metal-shader-table.h"
#include "metal-device.h"

namespace rhi::metal {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(
    Pipeline* pipeline,
    TransientResourceHeap* transientHeap,
    IRayTracingPassEncoder* encoder
)
{
    return RefPtr<Buffer>(0);
}

} // namespace rhi::metal

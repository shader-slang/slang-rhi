#include "metal-shader-table.h"
#include "metal-device.h"

namespace rhi::metal {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(
    PipelineBase* pipeline,
    TransientResourceHeapBase* transientHeap,
    IRayTracingCommandEncoder* encoder
)
{
    return RefPtr<Buffer>(0);
}

} // namespace rhi::metal

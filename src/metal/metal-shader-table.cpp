#include "metal-shader-table.h"
#include "metal-device.h"

namespace rhi::metal {

RefPtr<BufferResource> ShaderTableImpl::createDeviceBuffer(
    PipelineStateBase* pipeline,
    TransientResourceHeapBase* transientHeap,
    IResourceCommandEncoder* encoder
)
{
    return RefPtr<BufferResource>(0);
}

} // namespace rhi::metal

#pragma once

#include "../simple-transient-resource-heap.h"
#include "metal-base.h"
#include "metal-command-encoder.h"
#include "metal-shader-object.h"

namespace rhi::metal {

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    NS::SharedPtr<MTL::CommandBuffer> m_commandBuffer;
    std::vector<RefPtr<RefObject>> m_resources;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal

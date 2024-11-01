#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugCommandQueue : public DebugObject<ICommandQueue>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugCommandQueue);

public:
    ICommandQueue* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;
    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

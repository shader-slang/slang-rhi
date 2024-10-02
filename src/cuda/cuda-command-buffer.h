#pragma once

#include "cuda-base.h"
#include "../command-writer.h"

namespace rhi::cuda {

class CommandBufferImpl : public ICommandBuffer, public CommandWriter, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda

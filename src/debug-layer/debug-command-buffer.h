#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugCommandBuffer : public DebugObject<ICommandBuffer>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugCommandBuffer);

public:
    ICommandBuffer* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

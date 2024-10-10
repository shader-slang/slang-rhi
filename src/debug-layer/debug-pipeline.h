#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugPipeline : public DebugObject<IPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugPipeline);

public:
    IPipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

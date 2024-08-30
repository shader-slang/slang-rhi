#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugPipelineState : public DebugObject<IPipelineState>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IPipelineState* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

} // namespace rhi::debug

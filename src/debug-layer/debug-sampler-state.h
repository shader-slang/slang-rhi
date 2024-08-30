#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugSamplerState : public DebugObject<ISamplerState>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    ISamplerState* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outNativeHandle) override;
};

} // namespace rhi::debug

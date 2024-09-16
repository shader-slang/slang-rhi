#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugSampler : public DebugObject<ISampler>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    ISampler* getInterface(const Guid& guid);

    virtual SLANG_NO_THROW const SamplerDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugTexture : public DebugObject<ITexture>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugTexture);

public:
    ITexture* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW const TextureDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

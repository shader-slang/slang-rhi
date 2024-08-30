#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugTexture : public DebugObject<ITexture>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    ITexture* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW TextureDesc* SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setDebugName(const char* name) override;
    virtual SLANG_NO_THROW const char* SLANG_MCALL getDebugName() override;
};

} // namespace rhi::debug

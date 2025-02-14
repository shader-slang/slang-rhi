#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugSurface : public DebugObject<ISurface>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugSurface);

    ISurface* getInterface(const Guid& guid);

public:
    bool m_configured = false;

    virtual SLANG_NO_THROW const SurfaceInfo& SLANG_MCALL getInfo() override;
    virtual SLANG_NO_THROW const SurfaceConfig& SLANG_MCALL getConfig() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::debug

#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugSurface : public DebugObject<ISurface>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    ISurface* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugSurface);

public:
    virtual SLANG_NO_THROW const SurfaceInfo& SLANG_MCALL getInfo() override;
    virtual SLANG_NO_THROW const SurfaceConfig* SLANG_MCALL getConfig() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;

public:
    bool m_configured = false;

    enum class State
    {
        Initial,
        ImageAcquired,
        ImagePresented,
    };

    State m_state = State::Initial;
};

} // namespace rhi::debug

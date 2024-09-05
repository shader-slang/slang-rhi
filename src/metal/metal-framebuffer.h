#pragma once

#include "metal-base.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::metal {

enum
{
    kMaxRenderTargets = 8,
    kMaxTargets = kMaxRenderTargets + 1,
};

class FramebufferLayoutImpl : public FramebufferLayoutBase
{
public:
    FramebufferLayoutDesc m_desc;

public:
    Result init(const FramebufferLayoutDesc& desc);
};

class FramebufferImpl : public FramebufferBase
{
public:
    BreakableReference<DeviceImpl> m_device;
    RefPtr<FramebufferLayoutImpl> m_layout;
    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_sampleCount;

public:
    Result init(DeviceImpl* device, const IFramebuffer::Desc& desc);
};

} // namespace rhi::metal

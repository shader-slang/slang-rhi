#pragma once

#include "d3d11-base.h"

#include "core/short_vector.h"

namespace rhi::d3d11 {

enum
{
    kMaxUAVs = 64,
    kMaxRTVs = 8,
};

class FramebufferLayoutImpl : public FramebufferLayoutBase
{
public:
    FramebufferLayoutDesc m_desc;
};

} // namespace rhi::d3d11

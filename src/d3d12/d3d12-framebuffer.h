#pragma once

#include "d3d12-base.h"
#include "d3d12-resource-views.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

class FramebufferLayoutImpl : public FramebufferLayoutBase
{
public:
    FramebufferLayoutDesc m_desc;
};

} // namespace rhi::d3d12

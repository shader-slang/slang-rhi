#pragma once

// Implementation of a dummy render pass layout object that stores and holds its
// desc value. Used by targets that does not expose an API object for the render pass
// concept.

#include <slang-rhi.h>

#include "core/common.h"
#include "core/short_vector.h"

namespace rhi {

class SimpleRenderPassLayout : public IRenderPassLayout, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IRenderPassLayout* getInterface(const Guid& guid);

public:
    short_vector<TargetAccessDesc> m_renderTargetAccesses;
    TargetAccessDesc m_depthStencilAccess;
    bool m_hasDepthStencil;
    void init(const IRenderPassLayout::Desc& desc);
};

} // namespace rhi

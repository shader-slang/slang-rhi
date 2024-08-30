#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class ScopeNVAPI
{
public:
    ScopeNVAPI()
        : m_renderer(nullptr)
    {
    }
    SlangResult init(DeviceImpl* renderer, Index regIndex);
    ~ScopeNVAPI();

protected:
    DeviceImpl* m_renderer;
};

} // namespace rhi::d3d11

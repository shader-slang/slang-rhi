#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class ScopeNVAPI
{
public:
    ScopeNVAPI()
        : m_device(nullptr)
    {
    }
    Result init(DeviceImpl* device, Index regIndex);
    ~ScopeNVAPI();

protected:
    DeviceImpl* m_device;
};

} // namespace rhi::d3d11

#pragma once

#include "cpu-base.h"
#include "../backend.h"

namespace rhi::cpu {

class BackendImpl : public Backend
{
public:
    // Backend implementation

    IAdapter* getAdapter(uint32_t index) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

protected:
    Result enumerateAdapters() override;

private:
    Adapter m_adapter;
};

} // namespace rhi::cpu

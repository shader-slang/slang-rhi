#pragma once

#include "reference.h"
#include "rhi-shared-fwd.h"

namespace rhi {

class DeviceChild : public ComObject
{
public:
    DeviceChild(Device* device);
    virtual ~DeviceChild();

    template<typename T = Device>
    T* getDevice()
    {
        return static_cast<T*>(m_device.get());
    }

    void breakStrongReferenceToDevice();
    void establishStrongReferenceToDevice();

protected:
    BreakableReference<Device> m_device;
};

} // namespace rhi

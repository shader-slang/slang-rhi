#pragma once

#include "reference.h"
#include "rhi-shared-fwd.h"

namespace rhi {

class DeviceChild : public ComObject
{
public:
    DeviceChild(Device* device);

    template<typename T = Device>
    T* getDevice()
    {
        return static_cast<T*>(m_device.get());
    }

    uint64_t getUID() const { return m_uid; }

    void breakStrongReferenceToDevice();
    void establishStrongReferenceToDevice();

protected:
    BreakableReference<Device> m_device;
    uint64_t m_uid;
};

} // namespace rhi

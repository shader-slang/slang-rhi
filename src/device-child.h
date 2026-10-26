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
        return static_cast<T*>(m_device);
    }

protected:
    RefObject* getLifetimeOwner() const noexcept override;

    // RefObject pins this device for each nonempty external lifetime. Internal owners
    // must keep the device valid independently (including during device shutdown).
    Device* m_device;
};

} // namespace rhi

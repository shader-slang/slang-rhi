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

    // A device child that is only referenced internally by the RHI is, by construction, owned
    // by something the device itself owns, so its reference to the device closes a cycle and
    // must be weakened. Conversely, as soon as an external reference appears the child must
    // keep the device alive for as long as the holder can use it.
    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    void breakStrongReferenceToDevice();
    void establishStrongReferenceToDevice();

protected:
    BreakableReference<Device> m_device;
};

} // namespace rhi

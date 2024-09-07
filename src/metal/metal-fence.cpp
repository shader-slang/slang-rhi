#include "metal-fence.h"
#include "metal-device.h"

namespace rhi::metal {

FenceImpl::~FenceImpl() {}

Result FenceImpl::init(DeviceImpl* device, const FenceDesc& desc)
{
    m_device = device;
    m_event = NS::TransferPtr(m_device->m_device->newSharedEvent());
    if (!m_event)
    {
        return SLANG_FAIL;
    }
    m_event->setSignaledValue(desc.initialValue);
    return SLANG_OK;
}

Result FenceImpl::getCurrentValue(uint64_t* outValue)
{
    *outValue = m_event->signaledValue();
    return SLANG_OK;
}

Result FenceImpl::setCurrentValue(uint64_t value)
{
    m_event->setSignaledValue(value);
    return SLANG_OK;
}

Result FenceImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLSharedEvent;
    outHandle->value = (uint64_t)m_event.get();
    return SLANG_OK;
}

Result FenceImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::metal

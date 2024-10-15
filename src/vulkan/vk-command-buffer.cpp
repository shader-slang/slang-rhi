#include "vk-command-buffer.h"
#include "vk-device.h"
#include "vk-shader-object.h"
#include "vk-util.h"

namespace rhi::vk {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

#if 0

void CommandBufferImpl::comFree()
{
    m_transientHeap.breakStrongReference();
}

Result CommandBufferImpl::init(DeviceImpl* device, VkCommandPool pool, TransientResourceHeapImpl* transientHeap)
{
    m_device = device;
    m_transientHeap = transientHeap;
    m_pool = pool;

    auto& api = device->m_api;
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    SLANG_VK_RETURN_ON_FAIL(api.vkAllocateCommandBuffers(api.m_device, &allocInfo, &m_commandBuffer));

    beginCommandBuffer();
    return SLANG_OK;
}

void CommandBufferImpl::beginCommandBuffer()
{
    auto& api = m_device->m_api;
    VkCommandBufferBeginInfo beginInfo =
        {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    api.vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (m_preCommandBuffer)
    {
        api.vkBeginCommandBuffer(m_preCommandBuffer, &beginInfo);
    }
    m_isPreCommandBufferEmpty = true;
}

Result CommandBufferImpl::createPreCommandBuffer()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    auto& api = m_device->m_api;
    SLANG_VK_RETURN_ON_FAIL(api.vkAllocateCommandBuffers(api.m_device, &allocInfo, &m_preCommandBuffer));
    VkCommandBufferBeginInfo beginInfo =
        {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    api.vkBeginCommandBuffer(m_preCommandBuffer, &beginInfo);
    return SLANG_OK;
}

VkCommandBuffer CommandBufferImpl::getPreCommandBuffer()
{
    m_isPreCommandBufferEmpty = false;
    if (m_preCommandBuffer)
        return m_preCommandBuffer;
    createPreCommandBuffer();
    return m_preCommandBuffer;
}


#endif

} // namespace rhi::vk

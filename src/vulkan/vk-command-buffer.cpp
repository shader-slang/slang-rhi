#include "vk-command-buffer.h"
#include "vk-device.h"
#include "vk-shader-object.h"
#include "vk-util.h"

namespace rhi::vk {

// There are a pair of cyclic references between a `TransientResourceHeap` and
// a `CommandBuffer` created from the heap. We need to break the cycle when
// the public reference count of a command buffer drops to 0.

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

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

Result CommandBufferImpl::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    m_resourceCommandEncoder.init(this);
    *outEncoder = &m_resourceCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder)
{
    m_renderCommandEncoder.init(this);
    SLANG_RETURN_ON_FAIL(m_renderCommandEncoder.beginPass(desc));
    *outEncoder = &m_renderCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    m_computeCommandEncoder.init(this);
    *outEncoder = &m_computeCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
    if (!m_device->m_api.vkCmdBuildAccelerationStructuresKHR)
        return SLANG_E_NOT_AVAILABLE;
    m_rayTracingCommandEncoder.init(this);
    *outEncoder = &m_rayTracingCommandEncoder;
    return SLANG_OK;
}

void CommandBufferImpl::close()
{
    auto& vkAPI = m_device->m_api;
    if (!m_isPreCommandBufferEmpty)
    {
        // `preCmdBuffer` contains buffer transfer commands for shader object
        // uniform buffers, and we need a memory barrier here to ensure the
        // transfers are visible to shaders.
        VkMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkAPI.vkCmdPipelineBarrier(
            m_preCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            1,
            &memBarrier,
            0,
            nullptr,
            0,
            nullptr
        );
        vkAPI.vkEndCommandBuffer(m_preCommandBuffer);
    }
    vkAPI.vkEndCommandBuffer(m_commandBuffer);
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

} // namespace rhi::vk

#pragma once

#include "vk-base.h"
#include "vk-framebuffer.h"

#include "core/stable_vector.h"

#include <string>

namespace rhi::vk {

class DeviceImpl : public RendererBase
{
public:
    // Renderer    implementation
    Result initVulkanInstanceAndDevice(const InteropHandle* handles, bool useValidationLayer);
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const Desc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getFormatSupportedResourceStates(Format format, ResourceStateSet* outStates) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createFramebufferLayout(const IFramebufferLayout::Desc& desc, IFramebufferLayout** outLayout) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createFramebuffer(const IFramebuffer::Desc& desc, IFramebuffer** outFramebuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPassLayout(const IRenderPassLayout::Desc& desc, IRenderPassLayout** outRenderPassLayout) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    SLANG_NO_THROW Result SLANG_MCALL createBufferImpl(
        const BufferDesc& desc,
        VkBufferUsageFlags additionalUsageFlag,
        const void* initData,
        IBuffer** outBuffer
    );
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(InteropHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, IResourceView::Desc const& desc, IResourceView** outView) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferView(IBuffer* buffer, IBuffer* counterBuffer, IResourceView::Desc const& desc, IResourceView** outView)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(IInputLayout::Desc const& desc, IInputLayout** outLayout) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayoutBase** outLayout
    ) override;
    virtual Result createShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;
    virtual Result createMutableShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outShaderTable) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createProgram(
        const IShaderProgram::Desc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createGraphicsPipelineState(const GraphicsPipelineStateDesc& desc, IPipelineState** outState) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipelineState(const ComputePipelineStateDesc& desc, IPipelineState** outState) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipelineState(const RayTracingPipelineStateDesc& desc, IPipelineState** outState) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createQueryPool(const IQueryPool::Desc& desc, IQueryPool** outPool) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ResourceState state, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructurePrebuildInfo(
        const IAccelerationStructure::BuildInputs& buildInputs,
        IAccelerationStructure::PrebuildInfo* outPrebuildInfo
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const IAccelerationStructure::CreateDesc& desc,
        IAccelerationStructure** outView
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const IFence::Desc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(GfxCount fenceCount, IFence** fences, uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    void waitForGpu();

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(InteropHandles* outHandles) override;

    ~DeviceImpl();

public:
    VkBool32 handleDebugMessage(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objType,
        uint64_t srcObject,
        Size location, // TODO: Is "location" still needed for this function?
        int32_t msgCode,
        const char* pLayerPrefix,
        const char* pMsg
    );

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objType,
        uint64_t srcObject,
        Size location, // TODO: Is "location" still needed? Calls handleDebugMessage() which doesn't use it
        int32_t msgCode,
        const char* pLayerPrefix,
        const char* pMsg,
        void* pUserData
    );

    void _transitionImageLayout(
        VkImage image,
        VkFormat format,
        const TextureDesc& desc,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    );
    void _transitionImageLayout(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkFormat format,
        const TextureDesc& desc,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    );

    uint32_t getQueueFamilyIndex(ICommandQueue::QueueType queueType);

public:
    // DeviceImpl members.

    DeviceInfo m_info;
    std::string m_adapterName;

    VkDebugReportCallbackEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkDevice m_device = VK_NULL_HANDLE;

    VulkanModule m_module;
    VulkanApi m_api;

    VulkanDeviceQueue m_deviceQueue;
    uint32_t m_queueFamilyIndex;

    Desc m_desc;

    DescriptorSetAllocator descriptorSetAllocator;

    uint32_t m_queueAllocCount;

    // A list to hold objects that may have a strong back reference to the device
    // instance. Because of the pipeline cache in `RendererBase`, there could be a reference
    // cycle among `DeviceImpl`->`PipelineStateImpl`->`ShaderProgramImpl`->`DeviceImpl`.
    // Depending on whether a `PipelineState` objects gets stored in pipeline cache, there
    // may or may not be such a reference cycle.
    // We need to hold strong references to any objects that may become part of the reference
    // cycle here, so that when objects like `ShaderProgramImpl` lost all public refernces, we
    // can always safely break the strong reference in `ShaderProgramImpl::m_device` without
    // worrying the `ShaderProgramImpl` object getting destroyed after the completion of
    // `DeviceImpl::~DeviceImpl()'.
    stable_vector<RefPtr<RefObject>, 1024> m_deviceObjectsWithPotentialBackReferences;

    VkSampler m_defaultSampler;

    RefPtr<FramebufferImpl> m_emptyFramebuffer;
};

} // namespace rhi::vk

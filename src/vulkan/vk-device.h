#pragma once

#include "vk-base.h"

#include "core/stable_vector.h"

#include <string>

namespace rhi::vk {

class DeviceImpl : public Device
{
public:
    Result initVulkanInstanceAndDevice(const NativeHandle* handles, bool useValidationLayer);
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const Desc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;
    virtual Result createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;
    virtual Result createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outShaderTable) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

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

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(GfxCount fenceCount, IFence** fences, uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    void waitForGpu();

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(NativeHandles* outHandles) override;

    ~DeviceImpl();

public:
    VkBool32 handleDebugMessage(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData
    );

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    void _labelObject(uint64_t object, VkObjectType objectType, const char* label);

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

    VkDebugUtilsMessengerEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkDevice m_device = VK_NULL_HANDLE;

    VulkanModule m_module;
    VulkanApi m_api;

    VulkanDeviceQueue m_deviceQueue;
    uint32_t m_queueFamilyIndex;

    Desc m_desc;

    DescriptorSetAllocator descriptorSetAllocator;

    uint32_t m_queueAllocCount;

    // A list to hold objects that may have a strong back reference to the device
    // instance. Because of the pipeline cache in `Device`, there could be a reference
    // cycle among `DeviceImpl`->`PipelineImpl`->`ShaderProgramImpl`->`DeviceImpl`.
    // Depending on whether a `Pipeline` objects gets stored in pipeline cache, there
    // may or may not be such a reference cycle.
    // We need to hold strong references to any objects that may become part of the reference
    // cycle here, so that when objects like `ShaderProgramImpl` lost all public refernces, we
    // can always safely break the strong reference in `ShaderProgramImpl::m_device` without
    // worrying the `ShaderProgramImpl` object getting destroyed after the completion of
    // `DeviceImpl::~DeviceImpl()'.
    stable_vector<RefPtr<RefObject>, 1024> m_deviceObjectsWithPotentialBackReferences;

    VkSampler m_defaultSampler;
};

} // namespace rhi::vk

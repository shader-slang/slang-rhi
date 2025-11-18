#pragma once

#include "vk-base.h"
#include "vk-bindless-descriptor-set.h"

#include <string>

namespace rhi::vk {

class AdapterImpl : public Adapter
{
public:
    uint8_t m_deviceUUID[VK_UUID_SIZE];
};

class DeviceImpl : public Device
{
public:
    using Device::readBuffer;

    Result initVulkanInstanceAndDevice(
        const DeviceDesc& desc,
        bool enableValidationLayer,
        std::vector<Feature>& availableFeatures,
        std::vector<Capability>& availableCapabilities
    );
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBuffer(
        const BufferDesc& desc,
        const void* initData,
        IBuffer** outBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromNativeHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderTable(
        const ShaderTableDesc& desc,
        IShaderTable** outShaderTable
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline2(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline2(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline2(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createHeap(const HeapDesc& desc, IHeap** outHeap) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        Size* outSize,
        Size* outAlignment
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorProperties(
        CooperativeVectorProperties* properties,
        uint32_t* propertiesCount
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getCooperativeVectorMatrixSize(
        uint32_t rowCount,
        uint32_t colCount,
        CooperativeVectorComponentType componentType,
        CooperativeVectorMatrixLayout layout,
        size_t rowColumnStride,
        size_t* outSize
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL convertCooperativeVectorMatrix(
        void* dstBuffer,
        size_t dstBufferSize,
        const CooperativeVectorMatrixDesc* dstDescs,
        const void* srcBuffer,
        size_t srcBufferSize,
        const CooperativeVectorMatrixDesc* srcDescs,
        uint32_t matrixCount
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;

    void waitForGpu();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

    DeviceImpl();
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

    uint32_t getQueueFamilyIndex(QueueType queueType);

public:
    DeviceNativeHandles m_existingDeviceHandles;
    VulkanDeviceExtendedDesc m_extendedDesc;

    std::string m_adapterName;

    VkDebugUtilsMessengerEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkDevice m_device = VK_NULL_HANDLE;

    VulkanModule m_module;
    VulkanApi m_api;

    VulkanDeviceQueue m_deviceQueue;
    uint32_t m_queueFamilyIndex;
    RefPtr<CommandQueueImpl> m_queue;

    DescriptorSetAllocator descriptorSetAllocator;
    RefPtr<BindlessDescriptorSet> m_bindlessDescriptorSet;

    VkSampler m_defaultSampler;
};

} // namespace rhi::vk

namespace rhi {

IAdapter* getVKAdapter(uint32_t index);
Result createVKDevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi

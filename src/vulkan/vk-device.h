#pragma once

#include "vk-base.h"
#include "vk-bindless-descriptor-set.h"

#include <mutex>
#include <string>
#include <vector>

namespace rhi::vk {

class AdapterImpl : public Adapter
{
public:
    uint8_t m_deviceUUID[VK_UUID_SIZE];
};

struct CalibratedTimestampSupport
{
    bool available = false;
    const char* deviceExtensionName = nullptr;
    VkTimeDomainKHR hostTimeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
    CpuTimestampDomain cpuTimestampDomain = CpuTimestampDomain::Unknown;
    uint64_t cpuTimestampFrequency = 0;
};

class DeviceImpl : public Device
{
public:
    virtual bool canCreatePipelineOnTaskPool(const Pipeline* pipeline) const override
    {
        SLANG_UNUSED(pipeline);
        return true;
    }

    using Device::readBuffer;

    Result initVulkanInstance(
        const DeviceDesc& desc,
        const VulkanDeviceExtendedDesc* extendedDesc,
        const DebugLayerOptions& debugLayerOptions
    );
    Result initVulkanDevice(
        const DeviceDesc& desc,
        const VulkanDeviceExtendedDesc* extendedDesc,
        BackendImpl* backend,
        std::vector<Feature>& availableFeatures,
        std::vector<Capability>& availableCapabilities
    );

    Result initialize(const DeviceDesc& desc, BackendImpl* backend);
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromNativeHandle(
        NativeHandle handle,
        const TextureDesc& desc,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBuffer(
        const BufferDesc& desc,
        const void* initData,
        IBuffer** outBuffer
    ) override;

    /// Stage and submit initialization data for a newly created buffer.
    Result uploadBufferInitData(IBuffer* buffer, Offset offset, Size size, const void* data);

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

    virtual SLANG_NO_THROW Result SLANG_MCALL getMicromapSizes(
        const MicromapBuildDesc& desc,
        MicromapSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getClusterOperationSizes(
        const ClusterOperationParams& params,
        ClusterOperationSizes* outSizes
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createMicromap(
        const MicromapDesc& desc,
        IMicromap** outMicromap
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        Size* outSize,
        Size* outAlignment
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;
    virtual SLANG_NO_THROW Result getTextureBufferOffsetAlignment(Format format, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL isCooperativeMatrixSupported(
        const CooperativeMatrixDesc& desc,
        bool* outSupported
    ) override;

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

    void deferDelete(Resource* resource);

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

    /// If a shader called abort() (OpAbortKHR) the device is lost; retrieve the abort message via
    /// VK_KHR_device_fault and report it through the debug message callback. No-op when
    /// Feature::ShaderAbort is unavailable. Safe to call after VK_ERROR_DEVICE_LOST.
    void reportShaderAbortMessage();

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

    // --- Shared-resource queue-family ownership ping-pong (no public API) ---
    //
    // Vulkan shared resources are created VK_SHARING_MODE_EXCLUSIVE, so an external API (e.g. CUDA)
    // importing the underlying memory only observes the producer's writes after a queue-family
    // ownership transfer (QFOT) to VK_QUEUE_FAMILY_EXTERNAL. These resources ping-pong ownership
    // driven entirely by the app's existing calls: released to EXTERNAL on waitOnHost(), and
    // reclaimed by the producer before its next access (its next submit(), or a readBuffer). The
    // external consumer accesses the memory only after that host wait returns. Register/unregister
    // track the live shared resources; the release/acquire helpers record the QFOT barriers for
    // every registered resource in the matching state.
    void registerSharedBuffer(BufferImpl* buffer);
    void unregisterSharedBuffer(BufferImpl* buffer);
    void registerSharedTexture(TextureImpl* texture);
    void unregisterSharedTexture(TextureImpl* texture);
    void releaseSharedToExternal();
    void acquireSharedFromExternal();

public:
    DeviceNativeHandles m_existingDeviceHandles;

    std::string m_adapterName;

    VkDebugUtilsMessengerEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkDevice m_device = VK_NULL_HANDLE;
    bool m_hasSubgroupSizeControl = false;
    CalibratedTimestampSupport m_calibratedTimestampSupport;

    VulkanModule m_module;
    VulkanApi m_api;

    VulkanDeviceQueue m_deviceQueue;
    uint32_t m_queueFamilyIndex;

    struct CooperativeMatrixFlexibleProperty
    {
        uint32_t mGranularity = 0;
        uint32_t nGranularity = 0;
        uint32_t kGranularity = 0;
        CooperativeMatrixComponentType aType = CooperativeMatrixComponentType::Float16;
        CooperativeMatrixComponentType bType = CooperativeMatrixComponentType::Float16;
        CooperativeMatrixComponentType cType = CooperativeMatrixComponentType::Float16;
        CooperativeMatrixComponentType resultType = CooperativeMatrixComponentType::Float16;
        CooperativeMatrixScope scope = CooperativeMatrixScope::Subgroup;
    };

    bool m_cooperativeMatrixPropertiesInitialized = false;
    std::vector<CooperativeMatrixDesc> m_cooperativeMatrixFixedProperties;
    std::vector<CooperativeMatrixFlexibleProperty> m_cooperativeMatrixFlexibleProperties;
    RefPtr<CommandQueueImpl> m_queue;

    // Registry of live shared resources whose queue-family ownership ping-pongs with
    // VK_QUEUE_FAMILY_EXTERNAL. Raw pointers (not RefPtr) so registration does not keep a resource
    // alive; each entry is removed in the resource's destructor. Guarded by m_sharedResourceMutex,
    // matching the locking CommandQueueImpl uses for its own tracking lists.
    std::mutex m_sharedResourceMutex;
    std::vector<BufferImpl*> m_sharedBuffers;
    std::vector<TextureImpl*> m_sharedTextures;

    DescriptorSetAllocator descriptorSetAllocator;
    RefPtr<BindlessDescriptorSet> m_bindlessDescriptorSet;

    VkSampler m_defaultSampler;

#if SLANG_RHI_ENABLE_AFTERMATH
    /// Aftermath crash dumper (null if Aftermath is not enabled).
    AftermathCrashDumper* m_aftermathCrashDumper = nullptr;
#endif
};

} // namespace rhi::vk

#pragma once

#include <slang-com-helper.h>
#include <slang-rhi.h>

#if SLANG_WINDOWS_FAMILY
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif SLANG_APPLE_FAMILY
#define VK_USE_PLATFORM_METAL_EXT 1
#elif SLANG_LINUX_FAMILY
#define VK_USE_PLATFORM_XLIB_KHR 1
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

// Undef xlib macros
#ifdef Always
#undef Always
#endif
#ifdef None
#undef None
#endif

namespace rhi::vk {

struct VulkanModule
{
    /// true if has been initialized
    SLANG_FORCE_INLINE bool isInitialized() const { return m_module != nullptr; }

    /// Get a function by name
    PFN_vkVoidFunction getFunction(const char* name) const;

    /// Initialize
    Result init();
    /// Destroy
    void destroy();

    /// Dtor
    ~VulkanModule() { destroy(); }

protected:
    void* m_module = nullptr;
};

// clang-format off

#define VK_API_GLOBAL_PROCS(x) \
    x(vkGetInstanceProcAddr) \
    x(vkCreateInstance) \
    x(vkEnumerateInstanceLayerProperties) \
    x(vkEnumerateDeviceExtensionProperties) \
    x(vkDestroyInstance) \
    /* */

#define VK_API_INSTANCE_PROCS_OPT(x) \
    x(vkGetPhysicalDeviceFeatures2) \
    x(vkGetPhysicalDeviceProperties2) \
    x(vkCreateDebugUtilsMessengerEXT) \
    x(vkDestroyDebugUtilsMessengerEXT) \
    x(vkGetPhysicalDeviceCooperativeVectorPropertiesNV) \
    /* */

#define VK_API_INSTANCE_PROCS(x) \
    x(vkCreateDevice) \
    x(vkDestroyDevice) \
    x(vkEnumeratePhysicalDevices) \
    x(vkGetPhysicalDeviceProperties) \
    x(vkGetPhysicalDeviceFeatures) \
    x(vkGetPhysicalDeviceMemoryProperties) \
    x(vkGetPhysicalDeviceQueueFamilyProperties) \
    x(vkGetPhysicalDeviceFormatProperties) \
    x(vkGetPhysicalDeviceFormatProperties2) \
    x(vkGetPhysicalDeviceImageFormatProperties2) \
    x(vkGetDeviceProcAddr) \
    /* */

#define VK_API_DEVICE_PROCS(x) \
    x(vkCreateDescriptorPool) \
    x(vkDestroyDescriptorPool) \
    x(vkResetDescriptorPool) \
    x(vkGetDeviceQueue) \
    x(vkQueueSubmit) \
    x(vkQueueWaitIdle) \
    x(vkCreateBuffer) \
    x(vkAllocateMemory) \
    x(vkMapMemory) \
    x(vkUnmapMemory) \
    x(vkCmdCopyBuffer) \
    x(vkDestroyBuffer) \
    x(vkFreeMemory) \
    x(vkCreateDescriptorSetLayout) \
    x(vkDestroyDescriptorSetLayout) \
    x(vkAllocateDescriptorSets) \
    x(vkFreeDescriptorSets) \
    x(vkUpdateDescriptorSets) \
    x(vkCreatePipelineLayout) \
    x(vkDestroyPipelineLayout) \
    x(vkCreateComputePipelines) \
    x(vkCreateGraphicsPipelines) \
    x(vkDestroyPipeline) \
    x(vkCreateShaderModule) \
    x(vkDestroyShaderModule) \
    x(vkCreateFramebuffer) \
    x(vkDestroyFramebuffer) \
    x(vkCreateImage) \
    x(vkDestroyImage) \
    x(vkCreateImageView) \
    x(vkDestroyImageView) \
    x(vkCreateRenderPass) \
    x(vkDestroyRenderPass) \
    x(vkCreateCommandPool) \
    x(vkDestroyCommandPool) \
    x(vkCreateSampler) \
    x(vkDestroySampler) \
    x(vkCreateBufferView) \
    x(vkDestroyBufferView) \
    \
    x(vkGetBufferMemoryRequirements) \
    x(vkGetImageMemoryRequirements) \
    \
    x(vkCmdBindPipeline) \
    x(vkCmdClearAttachments) \
    x(vkCmdClearColorImage) \
    x(vkCmdClearDepthStencilImage) \
    x(vkCmdFillBuffer) \
    x(vkCmdBindDescriptorSets) \
    x(vkCmdDispatch) \
    x(vkCmdDispatchIndirect) \
    x(vkCmdDraw) \
    x(vkCmdDrawIndexed) \
    x(vkCmdDrawIndirect) \
    x(vkCmdDrawIndexedIndirect) \
    x(vkCmdDrawIndirectCount) \
    x(vkCmdDrawIndexedIndirectCount) \
    x(vkCmdSetScissor) \
    x(vkCmdSetViewport) \
    x(vkCmdBindVertexBuffers) \
    x(vkCmdBindIndexBuffer) \
    x(vkCmdBeginRenderPass) \
    x(vkCmdEndRenderPass) \
    x(vkCmdPipelineBarrier) \
    x(vkCmdCopyBufferToImage)\
    x(vkCmdCopyImage) \
    x(vkCmdCopyImageToBuffer) \
    x(vkCmdResolveImage) \
    x(vkCmdPushConstants) \
    x(vkCmdSetStencilReference) \
    x(vkCmdWriteTimestamp) \
    x(vkCmdBeginQuery) \
    x(vkCmdEndQuery) \
    x(vkCmdResetQueryPool) \
    x(vkCmdCopyQueryPoolResults) \
    \
    x(vkCreateFence) \
    x(vkDestroyFence) \
    x(vkResetFences) \
    x(vkGetFenceStatus) \
    x(vkWaitForFences) \
    \
    x(vkCreateSemaphore) \
    x(vkDestroySemaphore) \
    \
    x(vkCreateEvent) \
    x(vkDestroyEvent) \
    x(vkGetEventStatus) \
    x(vkSetEvent) \
    x(vkResetEvent) \
    \
    x(vkFreeCommandBuffers) \
    x(vkAllocateCommandBuffers) \
    x(vkBeginCommandBuffer) \
    x(vkEndCommandBuffer) \
    x(vkResetCommandBuffer) \
    x(vkResetCommandPool) \
    \
    x(vkBindImageMemory) \
    x(vkBindBufferMemory) \
    \
    x(vkCreateQueryPool) \
    x(vkGetQueryPoolResults) \
    x(vkDestroyQueryPool) \
    /* */

#if SLANG_WINDOWS_FAMILY
#   define VK_API_INSTANCE_PLATFORM_KHR_PROCS(x)          \
    x(vkCreateWin32SurfaceKHR) \
    /* */
#elif SLANG_APPLE_FAMILY
#   define VK_API_INSTANCE_PLATFORM_KHR_PROCS(x)          \
    x(vkCreateMetalSurfaceEXT) \
    /* */
#elif SLANG_LINUX_FAMILY
#   define VK_API_INSTANCE_PLATFORM_KHR_PROCS(x)          \
    x(vkCreateXlibSurfaceKHR) \
    /* */
#else
#   define VK_API_INSTANCE_PLATFORM_KHR_PROCS(x)          \
    /* */
#endif

#define VK_API_INSTANCE_KHR_PROCS(x)          \
    VK_API_INSTANCE_PLATFORM_KHR_PROCS(x) \
    x(vkGetPhysicalDeviceSurfaceSupportKHR) \
    x(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    x(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    x(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    x(vkDestroySurfaceKHR) \

    /* */

#define VK_API_DEVICE_KHR_PROCS(x) \
    x(vkQueuePresentKHR) \
    x(vkCreateSwapchainKHR) \
    x(vkGetSwapchainImagesKHR) \
    x(vkDestroySwapchainKHR) \
    x(vkAcquireNextImageKHR) \
    x(vkCmdBeginRenderingKHR) \
    x(vkCmdEndRenderingKHR) \
    x(vkCreateRayTracingPipelinesKHR) \
    x(vkCmdTraceRaysKHR) \
    x(vkGetRayTracingShaderGroupHandlesKHR) \
    /* */

#if SLANG_WINDOWS_FAMILY
#   define VK_API_DEVICE_PLATFORM_OPT_PROCS(x) \
    x(vkGetMemoryWin32HandleKHR) \
    x(vkGetSemaphoreWin32HandleKHR) \
    /* */
#else
#   define VK_API_DEVICE_PLATFORM_OPT_PROCS(x) \
    x(vkGetMemoryFdKHR) \
    x(vkGetSemaphoreFdKHR) \
    /* */
#endif

#define VK_API_DEVICE_OPT_PROCS(x) \
    VK_API_DEVICE_PLATFORM_OPT_PROCS(x) \
    x(vkCmdSetPrimitiveTopologyEXT) \
    x(vkGetBufferDeviceAddress) \
    x(vkGetBufferDeviceAddressKHR) \
    x(vkGetBufferDeviceAddressEXT) \
    x(vkCmdBuildAccelerationStructuresKHR) \
    x(vkCmdCopyAccelerationStructureKHR) \
    x(vkCmdCopyAccelerationStructureToMemoryKHR) \
    x(vkCmdCopyMemoryToAccelerationStructureKHR) \
    x(vkCmdWriteAccelerationStructuresPropertiesKHR) \
    x(vkCreateAccelerationStructureKHR) \
    x(vkDestroyAccelerationStructureKHR) \
    x(vkGetAccelerationStructureBuildSizesKHR) \
    x(vkGetSemaphoreCounterValue) \
    x(vkGetSemaphoreCounterValueKHR) \
    x(vkSignalSemaphore) \
    x(vkSignalSemaphoreKHR) \
    x(vkWaitSemaphores) \
    x(vkWaitSemaphoresKHR) \
    x(vkCmdSetSampleLocationsEXT) \
    x(vkCmdBeginDebugUtilsLabelEXT) \
    x(vkCmdEndDebugUtilsLabelEXT) \
    x(vkCmdInsertDebugUtilsLabelEXT) \
    x(vkSetDebugUtilsObjectNameEXT) \
    x(vkCmdDrawMeshTasksEXT) \
    x(vkConvertCooperativeVectorMatrixNV) \
    x(vkCmdConvertCooperativeVectorMatrixNV) \
    x(vkGetDescriptorSetLayoutSupport) \
    x(vkCreatePipelineBinariesKHR) \
    x(vkDestroyPipelineBinaryKHR) \
    x(vkGetPipelineBinaryDataKHR) \
    x(vkGetPipelineKeyKHR) \
    x(vkReleaseCapturedPipelineDataKHR) \
    /* */

#define VK_API_ALL_GLOBAL_PROCS(x) \
    VK_API_GLOBAL_PROCS(x)

#define VK_API_ALL_INSTANCE_PROCS(x) \
    VK_API_INSTANCE_PROCS(x) \
    VK_API_INSTANCE_KHR_PROCS(x)

#define VK_API_ALL_DEVICE_PROCS(x) \
    VK_API_DEVICE_PROCS(x) \
    VK_API_DEVICE_KHR_PROCS(x) \
    VK_API_DEVICE_OPT_PROCS(x)

#define VK_API_ALL_PROCS(x) \
    VK_API_ALL_GLOBAL_PROCS(x) \
    VK_API_ALL_INSTANCE_PROCS(x) \
    VK_API_ALL_DEVICE_PROCS(x) \
    \
    VK_API_INSTANCE_PROCS_OPT(x) \
    /* */

// clang-format on

#define VK_API_DECLARE_PROC(NAME) PFN_##NAME NAME = nullptr;

struct VulkanExtendedFeatures
{
    // 16 bit storage features
    VkPhysicalDevice16BitStorageFeatures storage16BitFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR
    };

    // Atomic Float features
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT
    };
    VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT atomicFloat2Features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT
    };

    // Image int64 atomic features
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT imageInt64AtomicFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT
    };

    // Extended dynamic state features
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT
    };

    // Acceleration structure features
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
    };

    // Ray tracing pipeline features
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
    };

    // Ray query (inline ray-tracing) features
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    // Ray tracing position fetch features
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR rayTracingPositionFetchFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR
    };

    // Inline uniform block features
    VkPhysicalDeviceInlineUniformBlockFeaturesEXT inlineUniformBlockFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT
    };

    // Robustness2 features
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT
    };

    // Ray tracing invocation reorder features
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV rayTracingInvocationReorderFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV
    };

    // Variable pointers features
    VkPhysicalDeviceVariablePointerFeaturesKHR variablePointersFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTER_FEATURES_KHR
    };

    // Compute shader derivatives features
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeShaderDerivativesFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR
    };

    // Clock features
    VkPhysicalDeviceShaderClockFeaturesKHR clockFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR
    };

    // Mesh shader features
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT
    };

    // Multiview features
    VkPhysicalDeviceMultiviewFeaturesKHR multiviewFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR};

    // Fragment shading rate features
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragmentShadingRateFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR
    };

    // Vulkan 1.2 features.
    VkPhysicalDeviceVulkan12Features vulkan12Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};

    // Vulkan 1.3 features.
    VkPhysicalDeviceVulkan13Features vulkan13Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

    // Vulkan 1.4 features.
    VkPhysicalDeviceVulkan14Features vulkan14Features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};

    // Draw parameters features
    VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES
    };

    // Dynamic rendering features
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR
    };

    // Custom border color features
    VkPhysicalDeviceCustomBorderColorFeaturesEXT customBorderColorFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT
    };

    // Dynamic rendering local read features
    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR dynamicRenderingLocalReadFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR
    };

    // 4444 formats features
    VkPhysicalDevice4444FormatsFeaturesEXT formats4444Features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT
    };

    // Ray tracing validation features
    VkPhysicalDeviceRayTracingValidationFeaturesNV rayTracingValidationFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV
    };

    // Maximal reconvergence features.
    VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR shaderMaximalReconvergenceFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR
    };

    // Quad control features.
    VkPhysicalDeviceShaderQuadControlFeaturesKHR shaderQuadControlFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR
    };

    // Integer dot product features.
    VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR shaderIntegerDotProductFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR,
    };

    // Cooperative vector features.
    VkPhysicalDeviceCooperativeVectorFeaturesNV cooperativeVectorFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV
    };

    // Linear swept spheres features.
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV rayTracingLinearSweptSpheresFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV
    };

    // Cooperative matrix 1 features.
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperativeMatrix1Features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR
    };

    // Descriptor indexing features
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    };

    // Mutable descriptor type features
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptorTypeFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT
    };

    // Pipeline binary features
    VkPhysicalDevicePipelineBinaryFeaturesKHR pipelineBinaryFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_BINARY_FEATURES_KHR
    };

    // Shader subgroup rotate features
    VkPhysicalDeviceShaderSubgroupRotateFeatures shaderSubgroupRotateFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR
    };

    // Shader replicated composites features
    VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT shaderReplicatedCompositesFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_REPLICATED_COMPOSITES_FEATURES_EXT
    };

    // Fragment shader barycentric features
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR fragmentShaderBarycentricFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR
    };

    // Vertex attribute robustness features
    VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT vertexAttributeRobustnessFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT
    };

    // Fragment shader interlock features
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlockFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT
    };

    // Shader demote to helper invocation features
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT shaderDemoteToHelperInvocationFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT
    };
};

struct VulkanApi
{
    VK_API_ALL_PROCS(VK_API_DECLARE_PROC)

    enum class ProcType
    {
        Global,
        Instance,
        Device,
    };

    /// Returns true if all the functions in the class are defined
    bool areDefined(ProcType type) const;

    /// Sets up global parameters
    Result initGlobalProcs(const VulkanModule& module);
    /// Initialize the instance functions
    Result initInstanceProcs(VkInstance instance);

    /// Called before initDevice
    Result initPhysicalDevice(VkPhysicalDevice physicalDevice);

    /// Initialize the device functions
    Result initDeviceProcs(VkDevice device);

    /// Type bits control which indices are tested against bit 0 for testing at index 0
    /// properties - a memory type must have all the bits set as passed in
    /// Returns -1 if couldn't find an appropriate memory type index
    int findMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    /// Given queue required flags, finds a queue
    int findQueue(VkQueueFlags reqFlags) const;

    /// Module this was all loaded from.
    const VulkanModule* m_module = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties m_deviceProperties;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingPipelineProperties;
    VkPhysicalDeviceFeatures m_deviceFeatures;
    VkPhysicalDeviceMemoryProperties m_deviceMemoryProperties;
    VulkanExtendedFeatures m_extendedFeatures;
};

} // namespace rhi::vk

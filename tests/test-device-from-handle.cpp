#include "testing.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-api.h"
#endif

#include <string>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

// Runs a trivial compute shader on the given device and checks the result.
static void checkComputeTrivial(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

GPU_TEST_CASE("device-from-handle", D3D12 | Vulkan | CUDA)
{
    DeviceNativeHandles handles;
    CHECK_CALL(device->getNativeDeviceHandles(&handles));

    ComPtr<IDevice> newDevice;
    DeviceDesc newDeviceDesc = {};
    newDeviceDesc.deviceType = device->getDeviceType();
    newDeviceDesc.existingDeviceHandles.handles[0] = handles.handles[0];
    if (newDeviceDesc.deviceType == DeviceType::Vulkan)
    {
        newDeviceDesc.existingDeviceHandles.handles[1] = handles.handles[1];
        newDeviceDesc.existingDeviceHandles.handles[2] = handles.handles[2];
    }
    newDeviceDesc.slang.slangGlobalSession = device->getSlangSession()->getGlobalSession();
    auto searchPaths = getSlangSearchPaths();
    newDeviceDesc.slang.searchPaths = searchPaths.data();
    newDeviceDesc.slang.searchPathCount = searchPaths.size();
    CHECK_CALL(getRHI()->createDevice(newDeviceDesc, newDevice.writeRef()));

    checkComputeTrivial(newDevice);
}

#if SLANG_RHI_ENABLE_VULKAN

namespace {

// Collects the error messages reported by a device.
class ErrorCollector : public IDebugCallback
{
public:
    std::vector<std::string> errors;

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        SLANG_UNUSED(source);
        if (type == DebugMessageType::Error)
            errors.push_back(message);
    }

    bool contains(const char* text) const
    {
        for (const auto& error : errors)
            if (error.find(text) != std::string::npos)
                return true;
        return false;
    }
};

// Creates VkDevices with a chosen set of extensions and features, on the instance and physical device of an existing
// slang-rhi device, and imports them into new slang-rhi devices.
class VulkanDeviceImporter
{
public:
    vk::VulkanApi api;

    ~VulkanDeviceImporter()
    {
        for (VkDevice vkDevice : m_vkDevices)
            api.vkDestroyDevice(vkDevice, nullptr);
    }

    Result init(IDevice* sourceDevice)
    {
        m_sourceDevice = sourceDevice;
        SLANG_RETURN_ON_FAIL(sourceDevice->getNativeDeviceHandles(&m_handles));
        SLANG_RETURN_ON_FAIL(m_module.init());
        SLANG_RETURN_ON_FAIL(api.initGlobalProcs(m_module));
        SLANG_RETURN_ON_FAIL(api.initInstanceProcs((VkInstance)m_handles.handles[0].value));
        SLANG_RETURN_ON_FAIL(api.initPhysicalDevice((VkPhysicalDevice)m_handles.handles[1].value));
        return SLANG_OK;
    }

    bool hasExtension(const char* name)
    {
        uint32_t count = 0;
        api.vkEnumerateDeviceExtensionProperties(api.m_physicalDevice, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> extensions(count);
        api.vkEnumerateDeviceExtensionProperties(api.m_physicalDevice, nullptr, &count, extensions.data());
        for (const auto& extension : extensions)
            if (std::string(extension.extensionName) == name)
                return true;
        return false;
    }

    // Creates a VkDevice with the given extensions and feature structures (the pNext chain of VkDeviceCreateInfo)
    // and imports it. The imported device must be released before the importer is destroyed.
    ComPtr<IDevice> createAndImport(
        const std::vector<const char*>& extensions,
        void* featureChain,
        IDebugCallback* debugCallback
    )
    {
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = uint32_t(api.findQueue(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT));
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo createInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext = featureChain;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.enabledExtensionCount = uint32_t(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDevice vkDevice = VK_NULL_HANDLE;
        REQUIRE(api.vkCreateDevice(api.m_physicalDevice, &createInfo, nullptr, &vkDevice) == VK_SUCCESS);
        m_vkDevices.push_back(vkDevice);

        VulkanDeviceExtendedDesc extendedDesc = {};
        extendedDesc.existingDeviceCreateInfo = &createInfo;

        DeviceDesc desc = {};
        desc.next = &extendedDesc;
        desc.deviceType = DeviceType::Vulkan;
        desc.existingDeviceHandles.handles[0] = m_handles.handles[0];
        desc.existingDeviceHandles.handles[1] = m_handles.handles[1];
        desc.existingDeviceHandles.handles[2].type = NativeHandleType::VkDevice;
        desc.existingDeviceHandles.handles[2].value = (uint64_t)vkDevice;
        desc.debugCallback = debugCallback;
        desc.slang.slangGlobalSession = m_sourceDevice->getSlangSession()->getGlobalSession();
        auto searchPaths = getSlangSearchPaths();
        desc.slang.searchPaths = searchPaths.data();
        desc.slang.searchPathCount = searchPaths.size();

        ComPtr<IDevice> device;
        getRHI()->createDevice(desc, device.writeRef());
        return device;
    }

private:
    IDevice* m_sourceDevice = nullptr;
    DeviceNativeHandles m_handles;
    vk::VulkanModule m_module;
    std::vector<VkDevice> m_vkDevices;
};

} // namespace

// Imports VkDevices that only have some of the supported features enabled, and checks that slang-rhi only reports
// (and uses) the features that were enabled.
GPU_TEST_CASE("device-from-handle-vulkan-enabled-features", Vulkan)
{
    // Declared first so that it outlives the imported devices.
    VulkanDeviceImporter importer;
    REQUIRE_CALL(importer.init(device));
    vk::VulkanApi& api = importer.api;

    if (api.m_deviceProperties.apiVersion < VK_API_VERSION_1_2)
        SKIP("requires Vulkan 1.2");

    VkPhysicalDeviceVulkan12Features supported12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 supported = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &supported12;
    api.vkGetPhysicalDeviceFeatures2(api.m_physicalDevice, &supported);
    REQUIRE(supported12.timelineSemaphore);

    ErrorCollector errors;
    ComPtr<IDevice> imported;

    SUBCASE("minimal")
    {
        // Only enable timeline semaphores, which slang-rhi requires.
        VkPhysicalDeviceVulkan12Features enabled12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enabled12.timelineSemaphore = VK_TRUE;
        imported = importer.createAndImport({}, &enabled12, &errors);
        REQUIRE(imported);

        for (Feature feature : {
                 Feature::Surface,
                 Feature::Double,
                 Feature::Half,
                 Feature::Int16,
                 Feature::Int64,
                 Feature::AtomicFloat,
                 Feature::AtomicInt64,
                 Feature::ShaderResourceMinLod,
                 Feature::Bindless,
                 Feature::CustomBorderColor,
                 Feature::ConservativeRasterization,
                 Feature::ViewportArrayIndex,
                 Feature::MultiView,
                 Feature::FragmentShadingRate,
                 Feature::Barycentrics,
                 Feature::MeshShader,
                 Feature::AccelerationStructure,
                 Feature::RayTracing,
                 Feature::RayQuery,
                 Feature::RealtimeClock,
                 Feature::CooperativeVector,
                 Feature::CooperativeMatrix,
                 Feature::TimestampCalibration,
                 Feature::PipelineCache,
             })
        {
            CAPTURE(feature);
            CHECK_FALSE(imported->hasFeature(feature));
        }
        for (Capability capability : {
                 Capability::spvMinLod,
                 Capability::spvInt64Atomics,
                 Capability::spvPhysicalStorageBufferKHR,
                 Capability::spvRayQueryKHR,
             })
        {
            CAPTURE(capability);
            CHECK_FALSE(imported->hasCapability(capability));
        }

        checkComputeTrivial(imported);
    }

    SUBCASE("selected")
    {
        // Enable a few of the supported features, both core and from an extension.
        VkPhysicalDeviceVulkan12Features enabled12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enabled12.timelineSemaphore = VK_TRUE;
        enabled12.shaderFloat16 = supported12.shaderFloat16;
        enabled12.shaderBufferInt64Atomics = supported12.shaderBufferInt64Atomics;
        enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
        VkPhysicalDeviceFeatures2 enabled = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        enabled.features.shaderInt64 = supported.features.shaderInt64;
        enabled.pNext = &enabled12;

        std::vector<const char*> extensions;
        VkPhysicalDeviceCustomBorderColorFeaturesEXT customBorderColor = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT
        };
        bool hasCustomBorderColor = false;
        if (importer.hasExtension(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME))
        {
            VkPhysicalDeviceCustomBorderColorFeaturesEXT supportedCustomBorderColor = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT
            };
            VkPhysicalDeviceFeatures2 query = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            query.pNext = &supportedCustomBorderColor;
            api.vkGetPhysicalDeviceFeatures2(api.m_physicalDevice, &query);
            if (supportedCustomBorderColor.customBorderColors)
            {
                customBorderColor.customBorderColors = VK_TRUE;
                customBorderColor.pNext = enabled12.pNext;
                enabled12.pNext = &customBorderColor;
                extensions.push_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
                hasCustomBorderColor = true;
            }
        }

        imported = importer.createAndImport(extensions, &enabled, &errors);
        REQUIRE(imported);

        CHECK(imported->hasFeature(Feature::Int64) == bool(supported.features.shaderInt64));
        CHECK(imported->hasFeature(Feature::Half) == bool(supported12.shaderFloat16));
        CHECK(imported->hasFeature(Feature::AtomicInt64) == bool(supported12.shaderBufferInt64Atomics));
        CHECK(
            imported->hasCapability(Capability::spvPhysicalStorageBufferKHR) == bool(supported12.bufferDeviceAddress)
        );
        CHECK(imported->hasFeature(Feature::CustomBorderColor) == hasCustomBorderColor);
        CHECK_FALSE(imported->hasFeature(Feature::Double));
        CHECK_FALSE(imported->hasFeature(Feature::Int16));

        checkComputeTrivial(imported);
    }

    SUBCASE("promoted-structures")
    {
        // Enable features through the structures of the extensions that were promoted to Vulkan 1.2,
        // instead of VkPhysicalDeviceVulkan12Features.
        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphore = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES
        };
        timelineSemaphore.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceShaderFloat16Int8Features float16Int8 = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES
        };
        float16Int8.shaderFloat16 = supported12.shaderFloat16;
        float16Int8.pNext = &timelineSemaphore;

        imported = importer.createAndImport({}, &float16Int8, &errors);
        REQUIRE(imported);

        CHECK(imported->hasFeature(Feature::Half) == bool(supported12.shaderFloat16));
        CHECK_FALSE(imported->hasFeature(Feature::AtomicInt64));

        checkComputeTrivial(imported);
    }

    SUBCASE("missing-timeline-semaphore")
    {
        // slang-rhi requires timeline semaphores, so importing a device without them must fail.
        imported = importer.createAndImport({}, nullptr, &errors);
        CHECK_FALSE(imported);
        CHECK(errors.contains("timelineSemaphore"));
        errors.errors.clear();
    }

    imported.setNull();
    for (const auto& error : errors.errors)
        MESSAGE(error);
    CHECK(errors.errors.empty());
}

#endif // SLANG_RHI_ENABLE_VULKAN

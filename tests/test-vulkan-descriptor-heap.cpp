#include "testing.h"
#include "test-ray-tracing-common.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-device.h"
#include "vulkan/vk-shader-program.h"
#include "vulkan/vk-shader-object-layout.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("vulkan-descriptor-heap-binding-mode", Vulkan)
{
    auto backend = checked_cast<vk::DeviceImpl*>(getUnderlyingDevice(device));
    CHECK_EQ(backend->m_useDescriptorHeap, options().vulkanDescriptorHeap);
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-bindless-buffers", "computeMain", program.writeRef()));
    auto pipeline = device->createComputePipeline({.program = program});
    REQUIRE(pipeline);
    auto impl = checked_cast<vk::ShaderProgramImpl*>(program.get());
    CHECK_EQ(impl->m_rootShaderObjectLayout->m_pipelineLayout == VK_NULL_HANDLE, options().vulkanDescriptorHeap);
    bool nativeHeap = false;
    for (const auto& module : impl->m_modules)
    {
        const uint32_t* words = static_cast<const uint32_t*>(module.code->getBufferPointer());
        size_t size = module.code->getBufferSize() / sizeof(uint32_t);
        for (size_t i = 5; i < size;)
        {
            uint32_t length = words[i] >> 16;
            REQUIRE(length > 0);
            REQUIRE(i + length <= size);
            // OpExtension declares the native interface emitted by Slang.
            if ((words[i] & 0xffff) == 10 && length > 1)
                nativeHeap |= std::strcmp(reinterpret_cast<const char*>(words + i + 1), "SPV_EXT_descriptor_heap") == 0;
            i += length;
        }
    }
    CHECK_EQ(nativeHeap, options().vulkanDescriptorHeap);
}

GPU_TEST_CASE("vulkan-descriptor-heap-exhaustion-and-reuse", Vulkan)
{
    auto backend = checked_cast<vk::DeviceImpl*>(getUnderlyingDevice(device));
    if (!backend->m_useDescriptorHeap)
        SKIP("run with -vulkan-descriptor-heap");
    BindlessDesc bindless = {};
    bindless.bufferCount = bindless.textureCount = bindless.samplerCount = 0;
    bindless.combinedTextureSamplerCount = bindless.accelerationStructureCount = 0;
    RefPtr<vk::DescriptorHeap> heap = new vk::DescriptorHeap(backend);
    REQUIRE_CALL(heap->initialize(bindless, 4));
    vk::DescriptorHeap::Allocation a, b, failed;
    REQUIRE_CALL(heap->allocate(2, 3, a));
    CHECK_EQ(heap->allocate(2, 2, failed), SLANG_E_OUT_OF_MEMORY);
    // A failed sampler allocation must return its resource allocation too.
    REQUIRE_CALL(heap->allocate(2, 1, b));
    heap->free(a);
    heap->free(b);
    REQUIRE_CALL(heap->allocate(4, 4, a));
    CHECK_EQ(heap->allocate(1, 0, failed), SLANG_E_OUT_OF_MEMORY);
    heap->free(a);
    REQUIRE_CALL(heap->allocate(4, 0, a));
    REQUIRE_CALL(heap->allocate(0, 4, b));
    heap->free(a);
    heap->free(b);
}

GPU_TEST_CASE("vulkan-descriptor-heap-bindless-acceleration-structure", Vulkan)
{
    if (!options().vulkanDescriptorHeap)
        SKIP("run with -vulkan-descriptor-heap");
    if (!device->hasFeature(Feature::RayQuery))
        SKIP("ray queries not supported");
    auto queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);
    DescriptorHandle handle = {};
    REQUIRE_CALL(tlas.tlas->getDescriptorHandle(&handle));


    const char* source = R"(
        uniform DescriptorHandle<RaytracingAccelerationStructure> scene;
        RaytracingAccelerationStructure boundScene;
        RWStructuredBuffer<uint> result;
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void computeMain()
        {
            RayDesc ray;
            ray.Origin = float3(0.25, 0.25, 0);
            ray.Direction = float3(0, 0, 1);
            ray.TMin = 0;
            ray.TMax = 10;
            RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
            RaytracingAccelerationStructure heapScene = ResourceDescriptorHeap[((uint2)scene).x];


            result[0] = ((uint2)scene).x;
            result[1] = 1;
        }
    )";
    auto session = device->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    auto module = session->loadModuleFromSourceString("heap_ray_query", "heap_ray_query", source, diagnostics.writeRef());
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module);
    ComPtr<slang::IEntryPoint> entry;
    REQUIRE_CALL(module->findEntryPointByName("computeMain", entry.writeRef()));
    slang::IComponentType* entries[] = {entry};
    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = entries;
    programDesc.slangEntryPointCount = 1;
    auto program = device->createShaderProgram(programDesc);
    REQUIRE(program);
    auto pipeline = device->createComputePipeline({.program = program});
    REQUIRE(pipeline);
    auto impl = checked_cast<vk::ShaderProgramImpl*>(program.get());
    for (const auto& moduleCode : impl->m_modules)
    {
        const uint32_t* words = static_cast<const uint32_t*>(moduleCode.code->getBufferPointer());
        size_t size = moduleCode.code->getBufferSize() / 4;
        for (size_t i = 5; i < size; i += words[i] >> 16)
        {
            if ((words[i] & 0xffff) == 71 && (words[i] >> 16) == 4 && words[i+2] == 6)
                fprintf(stderr, "STRIDE %u\n", words[i+3]);
            if ((words[i] & 0xffff) == 17)
                fprintf(stderr, "CAP %u\n", words[i+1]);
            if ((words[i] & 0xffff) == 10)
                fprintf(stderr, "EXT %s\n", reinterpret_cast<const char*>(words+i+1));
        }
    }
    BufferDesc bufferDesc = {};
    bufferDesc.size = 2 * sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto output = device->createBuffer(bufferDesc);
    REQUIRE(output);
    auto encoder = queue->createCommandEncoder();
    encoder->globalBarrier();
    auto pass = encoder->beginComputePass();
    auto root = pass->bindPipeline(pipeline);
    REQUIRE_CALL(ShaderCursor(root)["scene"].setDescriptorHandle(handle));
    REQUIRE_CALL(ShaderCursor(root)["boundScene"].setBinding(tlas.tlas));
    REQUIRE_CALL(ShaderCursor(root)["result"].setBinding(output));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, output, makeArray<uint32_t>(uint32_t(handle.value), 1));
}
#endif

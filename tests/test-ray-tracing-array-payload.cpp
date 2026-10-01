#include "testing.h"
#include "test-ray-tracing-common.h"

#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

enum class PayloadShape
{
    FlatFloat12,
    NestedMixed32,
};

// Compose the specified stage transformations independently of shader storage traversal.
uint32_t expectedPayloadWord(PayloadShape shape, uint32_t word, bool hit)
{
    const uint32_t field = shape == PayloadShape::FlatFloat12 ? 0 : word % 8;
    if (field < 4)
    {
        const float initial = float(word + 1) * 0.25f;
        // Hit: AnyHit doubles/adds word+3, then ClosestHit triples/subtracts word+1.
        const float result = hit ? 6.0f * initial + float(2 * word + 8) : -initial - float(32 + 2 * word);
        uint32_t bits;
        memcpy(&bits, &result, sizeof(bits));
        return bits;
    }
    if (field < 6)
    {
        const int32_t initial = -int32_t(word * 7 + 11);
        const int32_t result = hit ? 6 * initial - int32_t(2 * word + 6) : -initial + int32_t(word + 13);
        uint32_t bits;
        memcpy(&bits, &result, sizeof(bits));
        return bits;
    }
    const uint32_t initial = 0x81000000u + word * 17;
    if (!hit)
        return (initial ^ 0xa5a5a5a5u) + word * 5;
    const uint32_t afterAnyHit = (initial ^ 0x01010101u) + word * 3;
    return (afterAnyHit ^ (0x00200000u + word)) + word * 7;
}

// Payload arithmetic is deliberately stateful, so each primitive must invoke AnyHit at most once.
struct PayloadTriangleBLAS
{
    ComPtr<IBuffer> vertices;
    ComPtr<IBuffer> indices;
    ComPtr<IAccelerationStructure> blas;

    PayloadTriangleBLAS(IDevice* device, ICommandQueue* queue)
    {
        BufferDesc vertexDesc = {};
        vertexDesc.size = sizeof(SingleTriangleBLAS::kVertexData);
        vertexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        vertexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        vertices = device->createBuffer(vertexDesc, SingleTriangleBLAS::kVertexData);
        REQUIRE(vertices != nullptr);
        BufferDesc indexDesc = vertexDesc;
        indexDesc.size = sizeof(SingleTriangleBLAS::kIndexData);
        indices = device->createBuffer(indexDesc, SingleTriangleBLAS::kIndexData);
        REQUIRE(indices != nullptr);

        AccelerationStructureBuildInput input = {};
        input.type = AccelerationStructureBuildInputType::Triangles;
        input.triangles.vertexBuffers[0] = vertices;
        input.triangles.vertexBufferCount = 1;
        input.triangles.vertexFormat = Format::RGB32Float;
        input.triangles.vertexCount = SingleTriangleBLAS::kVertexCount;
        input.triangles.vertexStride = sizeof(Vertex);
        input.triangles.indexBuffer = indices;
        input.triangles.indexFormat = IndexFormat::Uint32;
        input.triangles.indexCount = SingleTriangleBLAS::kIndexCount;
        input.triangles.flags = AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation;
        AccelerationStructureBuildDesc build = {};
        build.inputs = &input;
        build.inputCount = 1;
        AccelerationStructureSizes sizes = {};
        REQUIRE_CALL(device->getAccelerationStructureSizes(build, &sizes));
        BufferDesc scratchDesc = {};
        scratchDesc.size = sizes.scratchSize;
        scratchDesc.usage = BufferUsage::UnorderedAccess;
        scratchDesc.defaultState = ResourceState::UnorderedAccess;
        auto scratch = device->createBuffer(scratchDesc);
        REQUIRE(scratch != nullptr);
        AccelerationStructureDesc desc = {};
        desc.kind = AccelerationStructureKind::BottomLevel;
        desc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(desc, blas.writeRef()));
        auto encoder = queue->createCommandEncoder();
        encoder->buildAccelerationStructure(build, blas, nullptr, scratch, 0, nullptr);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    }
};

// Share only the existing scene/dispatch setup; each payload has independent entry points.
void runArrayPayloadTest(GpuTestContext* ctx, PayloadShape shape)
{
    const bool flat = shape == PayloadShape::FlatFloat12;
    const uint32_t wordCount = flat ? 12 : 32;
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto device = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(device != nullptr);
        if (!device->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        auto queue = device->getQueue(QueueType::Graphics);
        PayloadTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);
        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-array-payload",
            {flat ? "flatRayGen" : "nestedRayGen"},
            {{flat ? "flatClosestHit" : "nestedClosestHit", flat ? "flatAnyHit" : "nestedAnyHit"}},
            {flat ? "flatMiss" : "nestedMiss"}
        );

        // One hit and one miss have disjoint outputs, each with every payload word observed.
        std::vector<uint32_t> initial(2 * wordCount + 2, 0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        const size_t byteCount = initial.size() * sizeof(uint32_t);
        BufferDesc desc = {};
        desc.size = byteCount;
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        auto output = device->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);

        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
        ShaderCursor cursor(rootObject);
        cursor["sceneBVH"].setBinding(tlas.tlas);
        cursor["arrayPayloadResults"].setBinding(output);
        pass->dispatchRays(0, 2, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());

        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, byteCount, blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), byteCount);
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[initial.size() - 1], initial.back());
        for (uint32_t ray = 0; ray < 2; ++ray)
        {
            CAPTURE(ray);
            for (uint32_t word = 0; word < wordCount; ++word)
            {
                CAPTURE(word);
                CHECK_EQ(actual[1 + ray * wordCount + word], expectedPayloadWord(shape, word, ray == 0));
            }
        }
    }
}

} // namespace

GPU_TEST_CASE("ray-tracing-array-payload-flat", CUDA | Vulkan | D3D12 | DontCreateDevice)
{
    runArrayPayloadTest(ctx, PayloadShape::FlatFloat12);
}

GPU_TEST_CASE("ray-tracing-array-payload-nested", CUDA | Vulkan | D3D12 | DontCreateDevice)
{
    runArrayPayloadTest(ctx, PayloadShape::NestedMixed32);
}

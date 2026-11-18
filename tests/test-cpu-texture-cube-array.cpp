// Tests for the CPU backend's texture array-coordinate classification. The coordinates after the
// base (u, v) pair select an array element: an explicit array index for the *Array types, or a
// cube face (the implicit array the base shape table describes). A cube array consumes both, the
// face first and then the cube index, giving element index face + 6 * cubeIndex. The plain-cube
// and one-element-array cases pin the behavior for shapes whose single element makes the array
// coordinate clamp to element 0.

#include "testing.h"

#include <slang-rhi-config.h>

#if SLANG_RHI_ENABLE_CPU

#include "../src/cpu/cpu-texture.h"

#include <vector>

using namespace rhi;
using namespace rhi::cpu;
using namespace rhi::testing;

namespace {

constexpr uint32_t kExtent = 4;
constexpr uint32_t kComponentsPerTexel = 4;
constexpr uint32_t kCubeFaceCount = 6;

// A single-mip RGBA32Float texture whose every texel of element `e` holds the value `e`, so a
// lookup returns the index of whichever element the coordinate math actually selected. The
// element count is derived from the same shape table TextureImpl::init uploads from, so the
// fixture's subresource list always matches the texture's layout.
struct ElementIndexedTexture
{
    TextureDesc desc;
    std::vector<float> storage;
    std::vector<SubresourceData> subresources;

    ElementIndexedTexture(TextureType type, Extent3D size, uint32_t arrayLength)
    {
        uint32_t elementCount = arrayLength * kCPUTextureBaseShapeInfos[(int)type].implicitArrayElementCount;

        desc = {};
        desc.type = type;
        desc.size = size;
        desc.arrayLength = arrayLength;
        desc.mipCount = 1;
        desc.format = Format::RGBA32Float;

        const uint32_t texelsPerElement = size.width * size.height * size.depth;
        const size_t texelRowSize = size.width * kComponentsPerTexel * sizeof(float);

        storage.resize(size_t(elementCount) * texelsPerElement * kComponentsPerTexel);
        subresources.resize(elementCount);
        for (uint32_t element = 0; element < elementCount; ++element)
        {
            float* elementData = &storage[size_t(element) * texelsPerElement * kComponentsPerTexel];
            for (uint32_t texel = 0; texel < texelsPerElement; ++texel)
            {
                for (uint32_t component = 0; component < kComponentsPerTexel; ++component)
                    elementData[texel * kComponentsPerTexel + component] = float(element);
            }

            subresources[element].data = elementData;
            subresources[element].rowPitch = texelRowSize;
            subresources[element].slicePitch = texelRowSize * size.height;
        }
    }

    // The subresource data pointers reference `storage`, so the fixture must not be copied and
    // `subresources` is valid only while this object (and its `storage`) is alive.
    ElementIndexedTexture(const ElementIndexedTexture&) = delete;
    ElementIndexedTexture& operator=(const ElementIndexedTexture&) = delete;
};

struct LookupCase
{
    // Coordinates in the order a compiled CPU-target shader passes them: base (u, v), then the cube
    // face, then (for cube arrays) the cube index.
    float sampleCoords[4];
    int32_t loadCoords[4];
    float expectedElement;
};

// Checks that Sample, SampleLevel and Load all read the expected array element. These are the
// slang_prelude entry points a compiled CPU-target shader calls.
void runLookups(TextureViewImpl& textureView, const LookupCase* lookups, size_t lookupCount)
{
    for (size_t i = 0; i < lookupCount; ++i)
    {
        const LookupCase& lookup = lookups[i];
        CAPTURE(i);
        CAPTURE(lookup.expectedElement);

        float sampled[kComponentsPerTexel] = {};
        textureView.Sample(slang_prelude::SamplerState{}, lookup.sampleCoords, sampled, sizeof(sampled));
        CHECK_EQ(sampled[0], lookup.expectedElement);

        float sampledLevel[kComponentsPerTexel] = {};
        textureView
            .SampleLevel(slang_prelude::SamplerState{}, lookup.sampleCoords, 0.0f, sampledLevel, sizeof(sampledLevel));
        CHECK_EQ(sampledLevel[0], lookup.expectedElement);

        float loaded[kComponentsPerTexel] = {};
        textureView.Load(lookup.loadCoords, loaded, sizeof(loaded));
        CHECK_EQ(loaded[0], lookup.expectedElement);
    }
}

LookupCase faceLookup(uint32_t face)
{
    return LookupCase{
        {0.5f, 0.5f, float(face), 0.0f},
        {1, 1, int32_t(face), 0},
        float(face),
    };
}

LookupCase cubeArrayLookup(uint32_t face, uint32_t cubeIndex)
{
    return LookupCase{
        {0.5f, 0.5f, float(face), float(cubeIndex)},
        {1, 1, int32_t(face), int32_t(cubeIndex)},
        float(face + kCubeFaceCount * cubeIndex),
    };
}

// Out-of-range coordinates clamp against their own dimension before combining: the face against
// the faces of one cube, the cube index against the array length.
LookupCase clampedCubeArrayLookup(int32_t face, int32_t cubeIndex, uint32_t arrayLength)
{
    face = std::min(std::max(face, 0), int32_t(kCubeFaceCount - 1));
    cubeIndex = std::min(std::max(cubeIndex, 0), int32_t(arrayLength - 1));
    return LookupCase{
        {0.5f, 0.5f, float(face), float(cubeIndex)},
        {1, 1, face, cubeIndex},
        float(face + kCubeFaceCount * cubeIndex),
    };
}

} // namespace

TEST_CASE("cpu-texture-cube-consumes-face-coordinate")
{
    ElementIndexedTexture fixture(TextureType::TextureCube, Extent3D{kExtent, kExtent, 1}, 1);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[kCubeFaceCount];
    for (uint32_t face = 0; face < kCubeFaceCount; ++face)
        lookups[face] = faceLookup(face);
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-cube-array-one-element-consumes-face-coordinate")
{
    ElementIndexedTexture fixture(TextureType::TextureCubeArray, Extent3D{kExtent, kExtent, 1}, 1);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[kCubeFaceCount];
    for (uint32_t face = 0; face < kCubeFaceCount; ++face)
        lookups[face] = faceLookup(face);
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-cube-array-consumes-face-and-cube-index")
{
    ElementIndexedTexture fixture(TextureType::TextureCubeArray, Extent3D{kExtent, kExtent, 1}, 2);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        cubeArrayLookup(5, 0),
        cubeArrayLookup(0, 1),
        cubeArrayLookup(2, 1),
        clampedCubeArrayLookup(9, 0, 2),
        clampedCubeArrayLookup(-3, 0, 2),
        clampedCubeArrayLookup(0, 100, 2),
        clampedCubeArrayLookup(2, -1, 2),
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-2dms-array-consumes-element-then-sample-coordinate")
{
    // For a Texture2DMSArray, Load consumes texelCoords[2] as the element index and
    // texelCoords[3] as the per-texel sample slot.
    ElementIndexedTexture fixture(TextureType::Texture2DMSArray, Extent3D{kExtent, kExtent, 1}, 1);
    fixture.desc.sampleCount = 2;
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        {{0.5f, 0.5f, 0.0f, 0.0f}, {1, 1, 0, 0}, 0.0f},
        {{0.5f, 0.5f, 0.0f, 0.0f}, {1, 1, 0, 1}, 0.0f},
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-2dms-array-honors-element-coordinate")
{
    ElementIndexedTexture fixture(TextureType::Texture2DMSArray, Extent3D{kExtent, kExtent, 1}, 2);
    fixture.desc.sampleCount = 2;
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        {{0.5f, 0.5f, 1.0f, 0.0f}, {1, 1, 1, 0}, 1.0f},
        {{0.5f, 0.5f, 0.0f, 0.0f}, {1, 1, 0, 1}, 0.0f},
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-2d-array-one-element-reads-element-0")
{
    ElementIndexedTexture fixture(TextureType::Texture2DArray, Extent3D{kExtent, kExtent, 1}, 1);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        {{0.5f, 0.5f, 0.0f, 0.0f}, {1, 1, 0, 0}, 0.0f},
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-1d-array-one-element-reads-element-0")
{
    ElementIndexedTexture fixture(TextureType::Texture1DArray, Extent3D{kExtent, 1, 1}, 1);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        {{0.5f, 0.0f, 0.0f, 0.0f}, {1, 0, 0, 0}, 0.0f},
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-cube-array-shader-sampling")
{
    // End-to-end: a compiled CPU-target shader samples the cube array through the same
    // slang_prelude entry points, checking that the coordinate packing the prelude produces
    // (u, v, face, cubeIndex) selects the elements the lookups above expect.
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = DeviceType::CPU;
    ComPtr<IDevice> device;
    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));

    ElementIndexedTexture fixture(TextureType::TextureCubeArray, Extent3D{kExtent, kExtent, 1}, 2);
    fixture.desc.usage = TextureUsage::ShaderResource;
    fixture.desc.defaultState = ResourceState::ShaderResource;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(fixture.desc, fixture.subresources.data(), texture.writeRef()));

    SamplerDesc samplerDesc = {};
    ComPtr<ISampler> sampler;
    REQUIRE_CALL(device->createSampler(samplerDesc, sampler.writeRef()));

    const char* shaderSource = R"(
        TextureCubeArray<float4> tex;
        SamplerState s;
        RWStructuredBuffer<float> results;

        [shader("compute")]
        [numthreads(1, 1, 1)]
        void computeMain(uint3 tid : SV_DispatchThreadID)
        {
            results[0] = tex.SampleLevel(s, float4(0.5, 0.5, 2.0, 1.0), 0.0).x;
            results[1] = tex.SampleLevel(s, float4(0.5, 0.5, 0.0, 1.0), 0.0).x;
            results[2] = tex.SampleLevel(s, float4(0.5, 0.5, 5.0, 0.0), 0.0).x;
        }
    )";
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadComputeProgramFromSource(device, shaderSource, shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    BufferDesc bufferDesc = {};
    bufferDesc.size = 3 * sizeof(float);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> resultsBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultsBuffer.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["tex"].setBinding(texture);
        cursor["s"].setBinding(sampler);
        cursor["results"].setBinding(resultsBuffer);
        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, resultsBuffer, makeArray<float>(8.0f, 6.0f, 5.0f));
}

#endif // SLANG_RHI_ENABLE_CPU

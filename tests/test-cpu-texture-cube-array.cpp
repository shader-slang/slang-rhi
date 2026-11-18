#include "testing.h"

#include <slang-rhi-config.h>

#if SLANG_RHI_ENABLE_CPU

#include "../src/cpu/cpu-texture.h"

#include <vector>

using namespace rhi;
using namespace rhi::cpu;

namespace {

constexpr uint32_t kExtent = 4;

// The CPU backend stores array elements (cube faces included) as the outermost dimension of the
// texture, so filling each element with its own index makes a lookup return the index of whichever
// element the coordinate math actually selected.
struct ElementIndexedTexture
{
    TextureDesc desc;
    std::vector<float> storage;
    std::vector<SubresourceData> subresources;

    ElementIndexedTexture(TextureType type, Extent3D size, uint32_t arrayLength, uint32_t elementCount)
    {
        desc = {};
        desc.type = type;
        desc.size = size;
        desc.arrayLength = arrayLength;
        desc.mipCount = 1;
        desc.format = Format::RGBA32Float;

        const uint32_t texelsPerElement = size.width * size.height * size.depth;
        const size_t texelRowSize = size.width * 4 * sizeof(float);

        storage.resize(size_t(elementCount) * texelsPerElement * 4);
        subresources.resize(elementCount);
        for (uint32_t element = 0; element < elementCount; ++element)
        {
            float* elementData = &storage[size_t(element) * texelsPerElement * 4];
            for (uint32_t texel = 0; texel < texelsPerElement; ++texel)
            {
                for (uint32_t component = 0; component < 4; ++component)
                    elementData[texel * 4 + component] = float(element);
            }

            subresources[element].data = elementData;
            subresources[element].rowPitch = texelRowSize;
            subresources[element].slicePitch = texelRowSize * size.height;
        }
    }
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
        CAPTURE(lookup.expectedElement);

        float sampled[4] = {};
        textureView.Sample(slang_prelude::SamplerState{}, lookup.sampleCoords, sampled, sizeof(sampled));
        CHECK_EQ(sampled[0], lookup.expectedElement);

        float sampledLevel[4] = {};
        textureView
            .SampleLevel(slang_prelude::SamplerState{}, lookup.sampleCoords, 0.0f, sampledLevel, sizeof(sampledLevel));
        CHECK_EQ(sampledLevel[0], lookup.expectedElement);

        float loaded[4] = {};
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
        float(face + 6 * cubeIndex),
    };
}

} // namespace

TEST_CASE("cpu-texture-cube-consumes-face-coordinate")
{
    ElementIndexedTexture fixture(TextureType::TextureCube, Extent3D{kExtent, kExtent, 1}, 1, 6);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[6];
    for (uint32_t face = 0; face < 6; ++face)
        lookups[face] = faceLookup(face);
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-cube-array-one-element-consumes-face-coordinate")
{
    ElementIndexedTexture fixture(TextureType::TextureCubeArray, Extent3D{kExtent, kExtent, 1}, 1, 6);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[6];
    for (uint32_t face = 0; face < 6; ++face)
        lookups[face] = faceLookup(face);
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-cube-array-consumes-face-and-cube-index")
{
    ElementIndexedTexture fixture(TextureType::TextureCubeArray, Extent3D{kExtent, kExtent, 1}, 2, 12);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    // The element index of a cube array is face + 6 * cubeIndex.
    LookupCase lookups[] = {
        cubeArrayLookup(5, 0),
        cubeArrayLookup(0, 1),
        cubeArrayLookup(2, 1),
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

TEST_CASE("cpu-texture-2d-array-one-element-reads-element-0")
{
    ElementIndexedTexture fixture(TextureType::Texture2DArray, Extent3D{kExtent, kExtent, 1}, 1, 1);
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
    ElementIndexedTexture fixture(TextureType::Texture1DArray, Extent3D{kExtent, 1, 1}, 1, 1);
    TextureImpl texture(nullptr, fixture.desc);
    REQUIRE_CALL(texture.init(fixture.subresources.data()));
    TextureViewImpl textureView(&texture, TextureViewDesc{});

    LookupCase lookups[] = {
        {{0.5f, 0.0f, 0.0f, 0.0f}, {1, 0, 0, 0}, 0.0f},
    };
    runLookups(textureView, lookups, SLANG_COUNT_OF(lookups));
}

#endif // SLANG_RHI_ENABLE_CPU

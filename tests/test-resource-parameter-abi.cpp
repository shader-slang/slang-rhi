#include "testing.h"

#include <algorithm>
#include <array>
#include <cstdint>

using namespace rhi;
using namespace rhi::testing;

namespace {

uint32_t floatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Check the entire allocation so a correct selected value cannot hide a neighboring write.
void checkBufferWords(IDevice* device, IBuffer* buffer, const uint32_t* expected, size_t count)
{
    ComPtr<ISlangBlob> blob;
    REQUIRE_CALL(device->readBuffer(buffer, 0, count * sizeof(uint32_t), blob.writeRef()));
    REQUIRE_EQ(blob->getBufferSize(), count * sizeof(uint32_t));
    const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
    for (size_t word = 0; word < count; ++word)
    {
        CAPTURE(word);
        CHECK_EQ(actual[word], expected[word]);
    }
}

// Row padding belongs to the readback API; every physical texel lane belongs to this oracle.
void checkTextureWords(
    IDevice* device,
    ITexture* texture,
    uint32_t layer,
    uint32_t width,
    uint32_t height,
    uint32_t lanes,
    const uint32_t* expected
)
{
    ComPtr<ISlangBlob> blob;
    SubresourceLayout layout;
    REQUIRE_CALL(device->readTexture(texture, layer, 0, blob.writeRef(), &layout));
    REQUIRE(layout.colPitch >= lanes * sizeof(uint32_t));
    REQUIRE(layout.rowPitch >= width * layout.colPitch);
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            CAPTURE(layer);
            CAPTURE(x);
            CAPTURE(y);
            size_t offset = y * layout.rowPitch + x * layout.colPitch;
            REQUIRE(offset + lanes * sizeof(uint32_t) <= blob->getBufferSize());
            for (uint32_t lane = 0; lane < lanes; ++lane)
            {
                CAPTURE(lane);
                uint32_t actual;
                memcpy(&actual, static_cast<const uint8_t*>(blob->getBufferPointer()) + offset + lane * 4, 4);
                CHECK_EQ(actual, expected[(y * width + x) * lanes + lane]);
            }
        }
    }
}

} // namespace

GPU_TEST_CASE("resource-parameter-abi", CUDA | DontCreateDevice)
{
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
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-resource-parameter-abi", "computeMain", program.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        auto pipeline = device->createComputePipeline(pipelineDesc);
        REQUIRE(pipeline != nullptr);

        constexpr const char* vectorNames[] = {"i2", "i3", "i4", "u2", "u3", "u4", "f2", "f3", "f4"};
        constexpr const char* tailNames[] = {
            "tailI2",
            "tailI3",
            "tailI4",
            "tailU2",
            "tailU3",
            "tailU4",
            "tailF2",
            "tailF3",
            "tailF4",
        };
        constexpr const char* resourceNames[] = {
            "rawRead",
            "rawWrite",
            "structuredRead",
            "structuredWrite",
            "sampled",
            "sampler",
            "surface",
            "combined1D",
            "combined1DArray",
        };
        std::array<std::array<uint32_t, 4>, 9> vectorBits = {};
        std::array<uint32_t, 9> tails;
        for (uint32_t group = 0; group < 9; ++group)
        {
            tails[group] = 0x61000000 + group * 0x101;
            for (uint32_t lane = 0; lane < 2 + group % 3; ++lane)
            {
                if (group < 3)
                    vectorBits[group][lane] = static_cast<uint32_t>(-31 - int32_t(group * 8 + lane));
                else if (group < 6)
                    vectorBits[group][lane] = 0x81000000 + group * 0x100 + lane;
                else
                    vectorBits[group][lane] = floatBits(-float(group * 8 + lane) * 0.5f - 0.25f);
            }
        }

        std::array<std::array<uint32_t, 8>, 4> bufferInputs;
        std::array<ComPtr<IBuffer>, 4> buffers;
        for (uint32_t binding = 0; binding < 4; ++binding)
        {
            for (uint32_t word = 0; word < 8; ++word)
                bufferInputs[binding][word] = 0x71000000 + binding * 0x1000 + word * 0x11;
            BufferDesc desc = {};
            desc.size = sizeof(bufferInputs[binding]);
            desc.elementSize = binding < 2 ? 0 : sizeof(uint32_t);
            desc.usage =
                BufferUsage::CopySource | (binding % 2 ? BufferUsage::UnorderedAccess : BufferUsage::ShaderResource);
            desc.defaultState = binding % 2 ? ResourceState::UnorderedAccess : ResourceState::ShaderResource;
            REQUIRE_CALL(device->createBuffer(desc, bufferInputs[binding].data(), buffers[binding].writeRef()));
        }

        std::array<float, 24> sampledInput;
        std::array<uint32_t, 24> sampledBits;
        std::array<uint32_t, 24> surfaceInput;
        for (uint32_t word = 0; word < 24; ++word)
        {
            sampledInput[word] = 100.0f + word * 0.5f;
            sampledBits[word] = floatBits(sampledInput[word]);
            surfaceInput[word] = 0x82000000 + word * 0x101;
        }
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::Texture2D;
        textureDesc.size = {3, 2, 1};
        textureDesc.format = Format::RGBA32Float;
        textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource;
        SubresourceData sampledData = {sampledInput.data(), 3 * 4 * sizeof(float), 0};
        ComPtr<ITexture> sampled;
        REQUIRE_CALL(device->createTexture(textureDesc, &sampledData, sampled.writeRef()));
        textureDesc.format = Format::RGBA32Uint;
        textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        SubresourceData surfaceData = {surfaceInput.data(), 3 * 4 * sizeof(uint32_t), 0};
        ComPtr<ITexture> surface;
        REQUIRE_CALL(device->createTexture(textureDesc, &surfaceData, surface.writeRef()));

        constexpr float oneDInput[] = {11.f, 23.f, 37.f, 53.f};
        constexpr float arrayInput[2][4] = {{101.f, 113.f, 127.f, 139.f}, {211.f, 223.f, 227.f, 239.f}};
        textureDesc.type = TextureType::Texture1D;
        textureDesc.size = {4, 1, 1};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource;
        SubresourceData oneDData = {oneDInput, sizeof(oneDInput), 0};
        ComPtr<ITexture> combined1D;
        REQUIRE_CALL(device->createTexture(textureDesc, &oneDData, combined1D.writeRef()));
        textureDesc.type = TextureType::Texture1DArray;
        textureDesc.arrayLength = 2;
        SubresourceData arrayData[] = {
            {arrayInput[0], sizeof(arrayInput[0]), 0},
            {arrayInput[1], sizeof(arrayInput[1]), 0}
        };
        ComPtr<ITexture> combined1DArray;
        REQUIRE_CALL(device->createTexture(textureDesc, arrayData, combined1DArray.writeRef()));
        SamplerDesc samplerDesc = {};
        samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = TextureFilteringMode::Point;
        auto sampler = device->createSampler(samplerDesc);
        REQUIRE(sampler != nullptr);

        // 36 vector/sentinel words, 16 buffer values, 6 dimensions, 8 texel lanes,
        // 6 combined samples, final scalar and completion, between two untouched guards.
        std::array<uint32_t, 76> initial;
        initial.fill(0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        auto expected = initial;
        size_t word = 1;
        for (uint32_t group = 0; group < 9; ++group)
        {
            for (uint32_t lane = 0; lane < 2 + group % 3; ++lane)
                expected[word++] = vectorBits[group][lane];
            expected[word++] = tails[group];
        }
        for (const auto& input : bufferInputs)
            for (uint32_t index : {1, 2, 4, 6})
                expected[word++] = input[index];
        for (uint32_t dimension : {32, 32, 8, 4, 8, 4})
            expected[word++] = dimension;
        for (uint32_t lane = 0; lane < 4; ++lane)
            expected[word++] = sampledBits[4 + lane];
        for (uint32_t lane = 0; lane < 4; ++lane)
            expected[word++] = surfaceInput[4 + lane];
        // Point samples address x=0/2 and layers=0/1, independently of returned shader values.
        for (float value : {11.f, 37.f, 101.f, 127.f, 211.f, 227.f})
            expected[word++] = floatBits(value);
        constexpr uint32_t finalTail = 0xdead7654;
        expected[word++] = finalTail;
        expected[word++] = 0xc001c0de;
        REQUIRE_EQ(word, expected.size() - 1);
        BufferDesc outputDesc = {};
        outputDesc.size = sizeof(initial);
        outputDesc.elementSize = sizeof(uint32_t);
        outputDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        auto output = device->createBuffer(outputDesc, initial.data());
        REQUIRE(output != nullptr);

        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        size_t expectedOffset = 0;
        for (uint32_t group = 0; group < 9; ++group)
        {
            CAPTURE(group);
            uint32_t lanes = 2 + group % 3;
            size_t size = lanes * sizeof(uint32_t);
            size_t alignment = lanes == 3 ? 4 : size;
            expectedOffset = (expectedOffset + alignment - 1) / alignment * alignment;
            auto value = cursor[vectorNames[group]];
            auto tail = cursor[tailNames[group]];
            REQUIRE(value.isValid());
            REQUIRE(tail.isValid());
            CHECK_EQ(value.m_offset.uniformOffset, expectedOffset);
            CHECK_EQ(value.getTypeLayout()->getSize(), size);
            CHECK_EQ(value.getTypeLayout()->getAlignment(), alignment);
            CHECK_EQ(tail.m_offset.uniformOffset, expectedOffset + size);
            REQUIRE_CALL(value.setData(vectorBits[group].data(), size));
            REQUIRE_CALL(tail.setData(tails[group]));
            expectedOffset += size + sizeof(uint32_t);
            expectedOffset = (expectedOffset + 7) / 8 * 8;
            auto resource = cursor[resourceNames[group]];
            REQUIRE(resource.isValid());
            CHECK_EQ(resource.m_offset.uniformOffset, expectedOffset);
            CHECK_EQ(resource.getTypeLayout()->getSize(), group < 4 ? 16 : 8);
            expectedOffset += group < 4 ? 16 : 8;
        }
        expectedOffset = (expectedOffset + 7) / 8 * 8;
        CHECK_EQ(cursor["output"].m_offset.uniformOffset, expectedOffset);
        CHECK_EQ(cursor["output"].getTypeLayout()->getSize(), 16);
        CHECK_EQ(cursor["finalTail"].m_offset.uniformOffset, expectedOffset + 16);
        // Check the last member fits; do not infer an incidental whole-struct tail padding rule.
        CHECK(cursor.getTypeLayout()->getSize() >= expectedOffset + 20);
        for (uint32_t binding = 0; binding < 4; ++binding)
            REQUIRE_CALL(cursor[resourceNames[binding]].setBinding(buffers[binding]));
        REQUIRE_CALL(cursor["sampled"].setBinding(sampled));
        REQUIRE_CALL(cursor["sampler"].setBinding(sampler));
        REQUIRE_CALL(cursor["surface"].setBinding(surface));
        REQUIRE_CALL(cursor["combined1D"].setBinding(Binding(combined1D, sampler)));
        REQUIRE_CALL(cursor["combined1DArray"].setBinding(Binding(combined1DArray, sampler)));
        REQUIRE_CALL(cursor["output"].setBinding(output));
        REQUIRE_CALL(cursor["finalTail"].setData(finalTail));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        checkBufferWords(device, output, expected.data(), expected.size());

        auto expectedBuffers = bufferInputs;
        for (uint32_t index = 2; index < 6; ++index)
        {
            expectedBuffers[1][index] = 0xb1000000 + index;
            expectedBuffers[3][index] = 0xb2000000 + index;
        }
        for (uint32_t binding = 0; binding < 4; ++binding)
        {
            CAPTURE(binding);
            checkBufferWords(device, buffers[binding], expectedBuffers[binding].data(), 8);
        }
        auto expectedSurface = surfaceInput;
        constexpr uint32_t store[] = {0xc1000011, 0xc2000022, 0xc3000033, 0xc4000044};
        for (uint32_t lane = 0; lane < 4; ++lane)
            expectedSurface[4 + lane] = store[lane];
        checkTextureWords(device, surface, 0, 3, 2, 4, expectedSurface.data());
        checkTextureWords(device, sampled, 0, 3, 2, 4, sampledBits.data());
        std::array<uint32_t, 4> oneDBits;
        for (uint32_t x = 0; x < 4; ++x)
            oneDBits[x] = floatBits(oneDInput[x]);
        checkTextureWords(device, combined1D, 0, 4, 1, 1, oneDBits.data());
        for (uint32_t layer = 0; layer < 2; ++layer)
        {
            std::array<uint32_t, 4> arrayBits;
            for (uint32_t x = 0; x < 4; ++x)
                arrayBits[x] = floatBits(arrayInput[layer][x]);
            checkTextureWords(device, combined1DArray, layer, 4, 1, 1, arrayBits.data());
        }
    }
}

GPU_TEST_CASE("numeric-entry-abi", CUDA | DontCreateDevice)
{
    // Family order matches the authored entry signature, not reflected/backend output values.
    constexpr const char* names[12][4] = {
        {"i8_1", "i8_2", "i8_3", "i8_4"},
        {"u8_1", "u8_2", "u8_3", "u8_4"},
        {"i16_1", "i16_2", "i16_3", "i16_4"},
        {"u16_1", "u16_2", "u16_3", "u16_4"},
        {"i32_1", "i32_2", "i32_3", "i32_4"},
        {"u32_1", "u32_2", "u32_3", "u32_4"},
        {"i64_1", "i64_2", "i64_3", "i64_4"},
        {"u64_1", "u64_2", "u64_3", "u64_4"},
        {"h_1", "h_2", "h_3", "h_4"},
        {"f_1", "f_2", "f_3", "f_4"},
        {"d_1", "d_2", "d_3", "d_4"},
        {"b_1", "b_2", "b_3", "b_4"},
    };
    constexpr const char* tailNames[12][4] = {
        {"tail_i8_1", "tail_i8_2", "tail_i8_3", "tail_i8_4"},
        {"tail_u8_1", "tail_u8_2", "tail_u8_3", "tail_u8_4"},
        {"tail_i16_1", "tail_i16_2", "tail_i16_3", "tail_i16_4"},
        {"tail_u16_1", "tail_u16_2", "tail_u16_3", "tail_u16_4"},
        {"tail_i32_1", "tail_i32_2", "tail_i32_3", "tail_i32_4"},
        {"tail_u32_1", "tail_u32_2", "tail_u32_3", "tail_u32_4"},
        {"tail_i64_1", "tail_i64_2", "tail_i64_3", "tail_i64_4"},
        {"tail_u64_1", "tail_u64_2", "tail_u64_3", "tail_u64_4"},
        {"tail_h_1", "tail_h_2", "tail_h_3", "tail_h_4"},
        {"tail_f_1", "tail_f_2", "tail_f_3", "tail_f_4"},
        {"tail_d_1", "tail_d_2", "tail_d_3", "tail_d_4"},
        {"tail_b_1", "tail_b_2", "tail_b_3", "tail_b_4"},
    };
    constexpr size_t elementSizes[] = {1, 1, 2, 2, 4, 4, 8, 8, 2, 4, 8, 1};
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
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-resource-parameter-abi", "numericEntryMain", program.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        auto pipeline = device->createComputePipeline(pipelineDesc);
        REQUIRE(pipeline != nullptr);

        // 120 semantic lanes + 48 UInt tails + completion, between two 64-bit guards.
        std::array<uint64_t, 171> initial;
        initial.fill(UINT64_C(0xa5a5a5a5a5a5a5a5));
        initial.front() = UINT64_C(0x13579bdf02468ace);
        initial.back() = UINT64_C(0xfedcba9876543210);
        auto expected = initial;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint64_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        auto output = device->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        size_t expectedOffset = 0;
        size_t word = 1;
        for (uint32_t family = 0; family < 12; ++family)
        {
            for (uint32_t lanes = 1; lanes <= 4; ++lanes)
            {
                CAPTURE(names[family][lanes - 1]);
                const size_t elementSize = elementSizes[family];
                size_t size = elementSize * lanes;
                size_t alignment = lanes == 3 ? elementSize : std::min(size_t(16), size);
                if (family == 8 && lanes >= 3)
                {
                    // CUDA Half3/Half4 are 8-byte component records aligned to four bytes.
                    size = 8;
                    alignment = 4;
                }
                expectedOffset = (expectedOffset + alignment - 1) / alignment * alignment;
                auto value = cursor[names[family][lanes - 1]];
                auto tail = cursor[tailNames[family][lanes - 1]];
                REQUIRE(value.isValid());
                REQUIRE(tail.isValid());
                CHECK_EQ(value.m_offset.uniformOffset, expectedOffset);
                CHECK_EQ(value.getTypeLayout()->getSize(), size);
                CHECK_EQ(value.getTypeLayout()->getAlignment(), alignment);
                expectedOffset += size;
                expectedOffset = (expectedOffset + 3) / 4 * 4;
                CHECK_EQ(tail.m_offset.uniformOffset, expectedOffset);
                expectedOffset += sizeof(uint32_t);

                std::array<uint8_t, 32> storage;
                storage.fill(0x5a); // Padding is initialized but is never a semantic lane.
                for (uint32_t lane = 0; lane < lanes; ++lane)
                {
                    uint64_t bits = 0;
                    if (family < 8 && family % 2 == 0)
                    {
                        // Negative signed values require sign extension, including nonzero high64 bits.
                        int64_t signedValue = family == 6 ? -INT64_C(0x1234567800) - lanes * 16 - lane
                                                          : -int64_t(17 + family * 8 + lanes * 4 + lane);
                        bits = static_cast<uint64_t>(signedValue);
                    }
                    else if (family < 8)
                    {
                        bits = (UINT64_C(1) << (elementSize * 8 - 1)) | (0x21 + lanes * 8 + lane);
                        if (family == 7)
                            bits |= UINT64_C(0x1234567800000000);
                    }
                    else if (family == 8)
                    {
                        // RN16 encodings of 1.25, -2.5, 3.75 and -4.25, with distinct exact low bits.
                        constexpr uint16_t halfBits[] = {0x3d00, 0xc100, 0x4380, 0xc440};
                        bits = halfBits[lane] + lanes;
                    }
                    else if (family == 9)
                    {
                        bits = floatBits(-float(lanes * 8 + lane) * 0.5f - 0.25f);
                    }
                    else if (family == 10)
                    {
                        // Exact binary fractions use both halves of the Double mantissa.
                        double number = -double(lanes * 8 + lane) - 0.5 - double(lane + 1) * 0x1p-35;
                        memcpy(&bits, &number, sizeof(bits));
                    }
                    else
                    {
                        // Only canonical 0/1 Bool bytes are supplied, with both values in each vector.
                        bits = (lanes + lane) % 2;
                    }
                    memcpy(storage.data() + lane * elementSize, &bits, elementSize);
                    expected[word++] = bits;
                }
                REQUIRE_CALL(value.setData(storage.data(), size));
                uint32_t tailValue = 0x62000000 + family * 0x100 + lanes;
                REQUIRE_CALL(tail.setData(tailValue));
                expected[word++] = tailValue;
            }
        }
        expected[word++] = 0xc001c0de;
        REQUIRE_EQ(word, expected.size() - 1);
        expectedOffset = (expectedOffset + 7) / 8 * 8;
        CHECK_EQ(cursor["output"].m_offset.uniformOffset, expectedOffset);
        CHECK_EQ(cursor["output"].getTypeLayout()->getSize(), 16);
        CHECK(cursor.getTypeLayout()->getSize() >= expectedOffset + 16);
        REQUIRE_CALL(cursor["output"].setBinding(output));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint64_t*>(blob->getBufferPointer());
        for (size_t index = 0; index < expected.size(); ++index)
        {
            CAPTURE(index);
            CHECK_EQ(actual[index], expected[index]);
        }
    }
}

GPU_TEST_CASE("surface-dimensions", CUDA | DontCreateDevice)
{
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
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-resource-parameter-abi", "surfaceDimensionsMain", program.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        auto pipeline = device->createComputePipeline(pipelineDesc);
        REQUIRE(pipeline != nullptr);

        struct SurfaceSpec
        {
            const char* name;
            TextureType type;
            Extent3D size;
            uint32_t layers;
            Format format;
        };
        // Unequal spatial dimensions and layer counts expose accidental attribute swaps.
        constexpr SurfaceSpec specs[] = {
            {"oneD", TextureType::Texture1D, {7, 1, 1}, 1, Format::R32Float},
            {"twoD", TextureType::Texture2D, {5, 3, 1}, 1, Format::R32Float},
            {"threeD", TextureType::Texture3D, {11, 4, 2}, 1, Format::R32Float},
            {"oneDArray", TextureType::Texture1DArray, {9, 1, 1}, 3, Format::R32Float},
            {"twoDArray", TextureType::Texture2DArray, {6, 5, 1}, 4, Format::R32Float},
            {"half2D", TextureType::Texture2D, {13, 7, 1}, 1, Format::R16Float},
        };
        std::array<ComPtr<ITexture>, 6> textures;
        for (size_t index = 0; index < textures.size(); ++index)
        {
            const auto& spec = specs[index];
            TextureDesc desc = {};
            desc.type = spec.type;
            desc.size = spec.size;
            desc.arrayLength = spec.layers;
            desc.format = spec.format;
            desc.usage = TextureUsage::UnorderedAccess;
            // No texel is read: this test observes only allocation dimensions through surface handles.
            REQUIRE_CALL(device->createTexture(desc, nullptr, textures[index].writeRef()));
        }
        std::array<uint32_t, 16> initial;
        initial.fill(0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        constexpr std::array<uint32_t, 16> expected = {
            0x13579bdf,
            7,
            5,
            3,
            11,
            4,
            2,
            9,
            3,
            6,
            5,
            4,
            13,
            7,
            0xc001c0de,
            0x2468ace0,
        };
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        auto output = device->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        for (size_t index = 0; index < textures.size(); ++index)
            REQUIRE_CALL(cursor[specs[index].name].setBinding(textures[index]));
        REQUIRE_CALL(cursor["output"].setBinding(output));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        checkBufferWords(device, output, expected.data(), expected.size());
    }
}


GPU_TEST_CASE("aggregate-entry-abi", CUDA | DontCreateDevice)
{
    constexpr const char* names[] = {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "h", "f", "d", "b"};
    constexpr size_t sizes[] = {1, 1, 2, 2, 4, 4, 8, 8, 2, 4, 8, 1};
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
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-resource-parameter-abi", "aggregateEntryMain", program.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        auto pipeline = device->createComputePipeline(pipelineDesc);
        REQUIRE(pipeline != nullptr);
        std::array<uint64_t, 520> initial;
        initial.fill(UINT64_C(0xa5a5a5a5a5a5a5a5));
        initial.front() = UINT64_C(0x13579bdf02468ace);
        initial.back() = UINT64_C(0xfedcba9876543210);
        auto expected = initial;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint64_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        auto output = device->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        size_t word = 1;
        uint32_t head = 0x12345678;
        REQUIRE_CALL(cursor["head"].setData(&head, sizeof(head)));
        expected[word++] = head;
        for (uint32_t family = 0; family < 12; ++family)
        {
            CAPTURE(names[family]);
            auto direct = cursor[(std::string("direct_") + names[family]).c_str()];
            auto aggregate = cursor[(std::string("aggregate_") + names[family]).c_str()];
            // Poison all padding through reflection, then overwrite only semantic scalar lanes.
            std::vector<uint8_t> poison(aggregate.getTypeLayout()->getSize(), 0x5a);
            REQUIRE_CALL(aggregate.setData(poison.data(), poison.size()));
            uint32_t serial = 0;
            auto writeScalar = [&](ShaderCursor field)
            {
                const uint64_t value = 3 + serial++;
                uint64_t bits = value;
                uint64_t result = value;
                if (family < 8 && family % 2 == 0)
                    bits = result = uint64_t(-int64_t(value + (family == 6 ? UINT64_C(0x1234567800) : 0)));
                else if (family < 8)
                    bits = result = (UINT64_C(1) << (sizes[family] * 8 - 1)) | value;
                else if (family == 8)
                {
                    // Exact integer Half values from 1 through 8, independently encoded by the host.
                    constexpr uint16_t halfBits[] = {0x3c00, 0x4000, 0x4200, 0x4400, 0x4500, 0x4600, 0x4700, 0x4800};
                    result = value % 8 + 1;
                    bits = halfBits[result - 1];
                }
                else if (family == 9)
                {
                    float payload = float(value);
                    memcpy(&bits, &payload, sizeof(payload));
                }
                else if (family == 10)
                {
                    double payload = double(value);
                    memcpy(&bits, &payload, sizeof(payload));
                }
                else
                    bits = result = value % 2;
                REQUIRE_CALL(field.setData(&bits, sizes[family]));
                expected[word++] = result;
            };
            for (uint32_t i = 0; i < 2; ++i)
                writeScalar(direct[i]);
            for (uint32_t r = 0; r < 2; ++r)
            {
                auto record = aggregate["records"][r];
                for (uint32_t i = 0; i < 2; ++i)
                    for (uint32_t j = 0; j < 3; ++j)
                        writeScalar(record["values"][i][j]);
                for (uint32_t i = 0; i < 2; ++i)
                    for (uint32_t j = 0; j < 3; ++j)
                        writeScalar(record["triples"][i][j]);
                for (uint32_t i = 0; i < 2; ++i)
                    writeScalar(record["pair"][i]);
                for (uint32_t i = 0; i < 4; ++i)
                    writeScalar(record["quad"][i]);
                writeScalar(record["one"][0]);
            }
            uint32_t tail = 0xabc00000 + family;
            REQUIRE_CALL(cursor[(std::string("tail_") + names[family]).c_str()].setData(&tail, sizeof(tail)));
            expected[word++] = tail;
        }
        const uint16_t halves[] = {0x3c00, 0x4000, 0x4200};
        const uint16_t halfTail[] = {0x4400, 0x4500};
        const uint32_t tag = 91;
        const uint32_t integers[] = {101, 102, 103, 104};
        const double doubles[] = {201, 202};
        auto mixed = cursor["mixed"];
        std::vector<uint8_t> mixedPadding(mixed.getTypeLayout()->getSize(), 0x5a);
        REQUIRE_CALL(mixed.setData(mixedPadding.data(), mixedPadding.size()));
        REQUIRE_CALL(mixed["halves"].setData(halves, sizeof(halves)));
        REQUIRE_CALL(mixed["tag"].setData(&tag, sizeof(tag)));
        REQUIRE_CALL(mixed["unchanged"]["integers"].setData(integers, sizeof(integers)));
        REQUIRE_CALL(mixed["unchanged"]["doubles"].setData(doubles, sizeof(doubles)));
        REQUIRE_CALL(mixed["tail"].setData(halfTail, sizeof(halfTail)));
        for (auto value : {1, 2, 3, 91, 101, 102, 103, 104, 201, 202, 4, 5})
            expected[word++] = value;
        const float rows[] = {11, 12, 13, 14, 15, 16};
        const float columns[] = {21, 22, 23, 24, 25, 26};
        REQUIRE_CALL(cursor["matrices"]["rows"].setData(rows, sizeof(rows)));
        REQUIRE_CALL(cursor["matrices"]["columns"].setData(columns, sizeof(columns)));
        for (uint64_t v = 11; v <= 16; ++v)
            expected[word++] = v;
        for (uint64_t v = 21; v <= 26; ++v)
            expected[word++] = v;
        expected[word++] = 0xc001c0de;
        REQUIRE_EQ(word, expected.size() - 1);
        REQUIRE_CALL(cursor["output"].setBinding(output));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        auto actual = static_cast<const uint64_t*>(blob->getBufferPointer());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            CAPTURE(i);
            CHECK_EQ(actual[i], expected[i]);
        }
    }
}

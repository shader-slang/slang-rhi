#include "testing.h"

#include "rhi-shared.h"

#include <array>
#include <limits>

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("shader-table-record-stride-validation")
{
    using Error = ShaderTable::RecordLayoutError;

    const ShaderTable::RecordLayout layout = {
        /*headerSize=*/32,
        /*alignment=*/32,
        /*maximumStride=*/4096,
    };
    Size stride = 0;

    CHECK(ShaderTable::calculateRecordStride(0, 0, layout, &stride) == Error::None);
    CHECK_EQ(stride, 32);

    // The largest unaligned payload that exactly fills the backend stride remains valid.
    CHECK(ShaderTable::calculateRecordStride(4064, 0, layout, &stride) == Error::None);
    CHECK_EQ(stride, 4096);

    // One additional byte must be rejected after alignment instead of being narrowed later.
    CHECK(ShaderTable::calculateRecordStride(4065, 0, layout, &stride) == Error::StrideTooLarge);

    const ShaderTable::RecordLayout smallLayout = {
        /*headerSize=*/32,
        /*alignment=*/32,
        /*maximumStride=*/64,
    };
    // A legacy overwrite still determines the record stride when no raw record data is present.
    ShaderRecordOverwrite overwrite = {};
    overwrite.offset = 57;
    overwrite.size = 8;
    const Size overwriteEnd = Size(overwrite.offset) + Size(overwrite.size);
    CHECK(ShaderTable::calculateRecordStride(0, overwriteEnd, smallLayout, &stride) == Error::StrideTooLarge);

    const ShaderTable::RecordLayout addressSpaceLayout = {
        /*headerSize=*/32,
        /*alignment=*/32,
        /*maximumStride=*/std::numeric_limits<Size>::max(),
    };
    const Size maximumSize = std::numeric_limits<Size>::max();
    CHECK(ShaderTable::calculateRecordStride(maximumSize - 31, 0, addressSpaceLayout, &stride) == Error::SizeOverflow);
    CHECK(
        ShaderTable::calculateRecordStride(maximumSize - 32, 0, addressSpaceLayout, &stride) == Error::AlignmentOverflow
    );
}

TEST_CASE("shader-table-checked-size-arithmetic")
{
    const Size maximumSize = std::numeric_limits<Size>::max();
    Size result = 0;

    CHECK(ShaderTable::tryAddSize(maximumSize - 7, 7, &result));
    CHECK_EQ(result, maximumSize);
    CHECK(!ShaderTable::tryAddSize(maximumSize - 7, 8, &result));

    CHECK(ShaderTable::tryMultiplySize(maximumSize / 8, 8, &result));
    CHECK_EQ(result, maximumSize - 7);
    CHECK(!ShaderTable::tryMultiplySize(maximumSize / 8 + 1, 8, &result));

    CHECK(ShaderTable::tryAlignSize(maximumSize - 15, 16, &result));
    CHECK_EQ(result, maximumSize - 15);
    CHECK(!ShaderTable::tryAlignSize(maximumSize - 14, 16, &result));

    if constexpr (sizeof(Size) > sizeof(uint32_t))
    {
        // This is the boundary that used to wrap in the backend materializers: one large record
        // selects a 4096-byte stride for the complete one-million-record section.
        CHECK(ShaderTable::tryMultiplySize(1u << 20, 4096, &result));
        CHECK_EQ(result, Size(1) << 32);
    }
}

TEST_CASE("shader-table-record-data-ownership")
{
    std::array<uint8_t, 20> sourceData;
    for (size_t i = 0; i < sourceData.size(); ++i)
        sourceData[i] = uint8_t(i + 1);
    const auto expectedData = sourceData;

    ShaderRecordData recordData = {sourceData.data(), sourceData.size()};
    ShaderRecordOverwrite overwrite = {};
    overwrite.offset = 36;
    overwrite.size = 4;
    overwrite.data[0] = 0xf0;
    overwrite.data[1] = 0xf1;
    overwrite.data[2] = 0xf2;
    overwrite.data[3] = 0xf3;

    const char* rayGenNames[] = {"rayGen"};
    const char* missNames[] = {"miss"};
    const char* hitGroupNames[] = {"hitGroup"};
    const char* callableNames[] = {"callable"};
    ShaderTableDesc desc = {};
    desc.rayGenShaderCount = SLANG_COUNT_OF(rayGenNames);
    desc.rayGenShaderEntryPointNames = rayGenNames;
    desc.missShaderCount = SLANG_COUNT_OF(missNames);
    desc.missShaderEntryPointNames = missNames;
    desc.missShaderRecordData = &recordData;
    desc.hitGroupCount = SLANG_COUNT_OF(hitGroupNames);
    desc.hitGroupNames = hitGroupNames;
    desc.hitGroupRecordOverwrites = &overwrite;
    desc.hitGroupRecordData = &recordData;
    desc.callableShaderCount = SLANG_COUNT_OF(callableNames);
    desc.callableShaderEntryPointNames = callableNames;
    desc.callableShaderRecordData = &recordData;

    ShaderTable table(nullptr, desc);

    // Shader-table creation must not retain the application pointer. Backends lazily materialize
    // their table buffers, potentially after the source storage has gone out of scope.
    sourceData.fill(0);
    const std::array<const std::vector<ShaderTable::OwnedRecord>*, 3> recordArrays = {
        &table.m_missRecords,
        &table.m_hitGroupRecords,
        &table.m_callableRecords,
    };
    for (const auto* records : recordArrays)
    {
        REQUIRE_EQ(records->size(), 1);
        REQUIRE_EQ((*records)[0].data.size(), expectedData.size());
        for (size_t i = 0; i < expectedData.size(); ++i)
            CHECK_EQ((*records)[0].data[i], expectedData[i]);
    }

    const auto& ownedRecord = table.m_hitGroupRecords[0];

    std::array<uint8_t, 64> destination = {};
    ownedRecord.writeData(destination.data(), 32);
    for (size_t i = 0; i < expectedData.size(); ++i)
    {
        const uint8_t expected = i >= 4 && i < 8 ? overwrite.data[i - 4] : expectedData[i];
        CHECK_EQ(destination[32 + i], expected);
    }

    CHECK_EQ(ownedRecord.getSize(32), 52);
    CHECK_EQ(ShaderTable::getMaxRecordSize(table.m_hitGroupRecords, 32), 52);
}

TEST_CASE("shader-table-variable-record-packing")
{
    constexpr Size kHeaderSize = 32;
    constexpr Size kLargeDataSize = 68;
    constexpr Size kSmallDataSize = 4;
    constexpr Size kRecordStride = 128;

    std::array<std::array<uint8_t, kLargeDataSize>, 3> largeData;
    std::array<std::array<uint8_t, kSmallDataSize>, 3> smallData;
    std::array<std::array<ShaderRecordData, 2>, 3> recordData;
    for (size_t category = 0; category < recordData.size(); ++category)
    {
        largeData[category].fill(uint8_t(0x10 + category));
        smallData[category].fill(uint8_t(0x20 + category));
        recordData[category][0] = {largeData[category].data(), largeData[category].size()};
        recordData[category][1] = {smallData[category].data(), smallData[category].size()};
    }

    const char* missNames[] = {"miss0", "miss1"};
    const char* hitGroupNames[] = {"hit0", "hit1"};
    const char* callableNames[] = {"callable0", "callable1"};
    ShaderTableDesc desc = {};
    desc.missShaderCount = SLANG_COUNT_OF(missNames);
    desc.missShaderEntryPointNames = missNames;
    desc.missShaderRecordData = recordData[0].data();
    desc.hitGroupCount = SLANG_COUNT_OF(hitGroupNames);
    desc.hitGroupNames = hitGroupNames;
    desc.hitGroupRecordData = recordData[1].data();
    desc.callableShaderCount = SLANG_COUNT_OF(callableNames);
    desc.callableShaderEntryPointNames = callableNames;
    desc.callableShaderRecordData = recordData[2].data();

    ShaderTable table(nullptr, desc);

    const std::array<const std::vector<ShaderTable::OwnedRecord>*, 3> categories = {
        &table.m_missRecords,
        &table.m_hitGroupRecords,
        &table.m_callableRecords,
    };
    for (size_t category = 0; category < categories.size(); ++category)
    {
        const auto& records = *categories[category];
        REQUIRE_EQ(records.size(), 2);
        CHECK_EQ(ShaderTable::getMaxRecordSize(records, kHeaderSize), kHeaderSize + kLargeDataSize);

        std::array<uint8_t, kRecordStride * 2> section = {};
        records[0].writeData(section.data(), kHeaderSize);
        records[1].writeData(section.data() + kRecordStride, kHeaderSize);

        for (Size i = 0; i < kLargeDataSize; ++i)
            CHECK_EQ(section[kHeaderSize + i], uint8_t(0x10 + category));
        for (Size i = 0; i < kSmallDataSize; ++i)
            CHECK_EQ(section[kRecordStride + kHeaderSize + i], uint8_t(0x20 + category));

        // The second record must start at the maximum stride selected by record zero; bytes between
        // its short payload and the section end remain zero padding.
        for (Size i = kRecordStride + kHeaderSize + kSmallDataSize; i < section.size(); ++i)
            CHECK_EQ(section[i], 0);
    }
}

GPU_TEST_CASE("shader-table-record-data-invalid", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-hitobject-intrinsics", "missNOP", program.writeRef()));

    const char* missShaderName = "missNOP";
    ShaderTableDesc desc = {};
    desc.program = program;
    desc.missShaderCount = 1;
    desc.missShaderEntryPointNames = &missShaderName;

    ShaderRecordData recordData = {nullptr, 1};
    desc.missShaderRecordData = &recordData;
    ComPtr<IShaderTable> shaderTable;
    Device* baseDevice = getUnderlyingDevice(device);
    IDebugCallback* savedDebugCallback = baseDevice->m_debugCallback;
    baseDevice->m_debugCallback = NullDebugCallback::getInstance();
    Result nullDataResult = baseDevice->createShaderTable(desc, shaderTable.writeRef());
    CHECK_EQ(shaderTable.get(), nullptr);

    // A non-null pointer verifies that the backend rejects the impossible size before trying to
    // copy from it or narrowing the native stride.
    uint8_t sourceData = 0;
    recordData = {&sourceData, std::numeric_limits<Size>::max()};
    Result oversizedDataResult = baseDevice->createShaderTable(desc, shaderTable.writeRef());
    baseDevice->m_debugCallback = savedDebugCallback;

    CHECK_EQ(nullDataResult, SLANG_E_INVALID_ARG);
    CHECK_EQ(oversizedDataResult, SLANG_E_INVALID_ARG);
    CHECK_EQ(shaderTable.get(), nullptr);
}

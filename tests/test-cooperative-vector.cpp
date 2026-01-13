#include "testing.h"
#include "core/common.h"

#include <cmath>
#include <set>

#define VERBOSE 0

#if VERBOSE
#define PRINT(...) printf(__VA_ARGS__);
#else
#define PRINT(...)
#endif

namespace rhi {
inline doctest::String toString(CooperativeVectorMatrixLayout value)
{
    return enumToString(value);
}

inline doctest::String toString(CooperativeVectorComponentType value)
{
    return enumToString(value);
}
} // namespace rhi

using namespace rhi;
using namespace rhi::testing;

inline size_t getCooperativeVectorComponentSize(CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Sint8:
    case CooperativeVectorComponentType::Uint8:
    case CooperativeVectorComponentType::Sint8Packed:
    case CooperativeVectorComponentType::Uint8Packed:
    case CooperativeVectorComponentType::FloatE4M3:
    case CooperativeVectorComponentType::FloatE5M2:
        return 1;
    case CooperativeVectorComponentType::Float16:
    case CooperativeVectorComponentType::Sint16:
    case CooperativeVectorComponentType::Uint16:
        return 2;
    case CooperativeVectorComponentType::Float32:
    case CooperativeVectorComponentType::Sint32:
    case CooperativeVectorComponentType::Uint32:
        return 4;
    case CooperativeVectorComponentType::Float64:
    case CooperativeVectorComponentType::Sint64:
    case CooperativeVectorComponentType::Uint64:
        return 8;
    }
    return 0;
}

inline size_t getTightRowColumnStride(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout
)
{
    size_t componentSize = getCooperativeVectorComponentSize(componentType);
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return componentSize * colCount;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return componentSize * rowCount;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        break;
    }
    return 0;
}

inline size_t computeExpectedSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    uint32_t rowColumnStride = 0
)
{
    size_t tightStride = getTightRowColumnStride(rowCount, colCount, componentType, layout);
    size_t stride = rowColumnStride != 0 ? rowColumnStride : tightStride;

    // The last row/column uses tight packing, not the padded stride.
    // Total size = (count - 1) * stride + tightStride
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return (rowCount - 1) * stride + tightStride;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return (colCount - 1) * stride + tightStride;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        // Optimal layouts are implementation-defined.
        break;
    }
    return 0;
}


GPU_TEST_CASE("cooperative-vector-properties", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    uint32_t propertiesCount = 0;
    REQUIRE_CALL(device->getCooperativeVectorProperties(nullptr, &propertiesCount));
    std::vector<CooperativeVectorProperties> properties(propertiesCount);
    REQUIRE_CALL(device->getCooperativeVectorProperties(properties.data(), &propertiesCount));

    CHECK(propertiesCount > 0);
}

GPU_TEST_CASE("cooperative-vector-get-matrix-size", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    bool isCUDA = device->getDeviceType() == DeviceType::CUDA;

    auto querySize = [&](uint32_t rowCount,
                         uint32_t colCount,
                         CooperativeVectorComponentType componentType,
                         CooperativeVectorMatrixLayout layout,
                         uint32_t rowColumnStride = 0)
    {
        size_t size = 0;
        REQUIRE_CALL(
            device->getCooperativeVectorMatrixSize(rowCount, colCount, componentType, layout, rowColumnStride, &size)
        );
        return size;
    };

    // Query cooperative vector properties to determine supported component types.
    uint32_t propertiesCount = 0;
    REQUIRE_CALL(device->getCooperativeVectorProperties(nullptr, &propertiesCount));
    std::vector<CooperativeVectorProperties> properties(propertiesCount);
    REQUIRE_CALL(device->getCooperativeVectorProperties(properties.data(), &propertiesCount));

    // Determine supported component types (Float32 is implicit).
    std::set<CooperativeVectorComponentType> supportedComponentTypes;
    supportedComponentTypes.emplace(CooperativeVectorComponentType::Float32);
    for (const auto& props : properties)
    {
        supportedComponentTypes.insert(props.matrixInterpretation);
    }

    // Determine supported component types for basic and optimal layout types.
    std::vector<CooperativeVectorComponentType> basicLayoutComponentTypes;
    std::vector<CooperativeVectorComponentType> optimalLayoutComponentTypes;
    for (CooperativeVectorComponentType type : supportedComponentTypes)
    {
        if (type == CooperativeVectorComponentType::FloatE4M3 || type == CooperativeVectorComponentType::FloatE5M2)
        {
            optimalLayoutComponentTypes.push_back(type);
            continue;
        }
        basicLayoutComponentTypes.push_back(type);
        // OptiX does not support Float32 for training/inferencing optimal layouts.
        if (type == CooperativeVectorComponentType::Float32 && isCUDA)
            continue;
        optimalLayoutComponentTypes.push_back(type);
    }

    std::vector<CooperativeVectorMatrixLayout> layouts = {
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorMatrixLayout::InferencingOptimal,
        CooperativeVectorMatrixLayout::TrainingOptimal,
    };

    for (CooperativeVectorMatrixLayout layout : layouts)
    {
        PRINT("Layout: %s\n", toString(layout).c_str());
        CAPTURE(layout);

        std::vector<CooperativeVectorComponentType> componentTypes =
            (layout == CooperativeVectorMatrixLayout::InferencingOptimal ||
             layout == CooperativeVectorMatrixLayout::TrainingOptimal)
                ? optimalLayoutComponentTypes
                : basicLayoutComponentTypes;
        for (CooperativeVectorComponentType type : componentTypes)
        {
            PRINT("  Component Type: %s\n", toString(type).c_str());
            CAPTURE(type);
            for (uint32_t rows : {1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 32, 33, 64, 127, 128})
            {
                CAPTURE(rows);
                for (uint32_t cols : {1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 32, 33, 64, 127, 128})
                {
                    CAPTURE(cols);
                    size_t size = querySize(rows, cols, type, layout);
                    size_t expectedSize = computeExpectedSize(rows, cols, type, layout);
                    PRINT("    rows=%d, cols=%d, size=%zd, expectedSize=%zd\n", rows, cols, size, expectedSize);
                    if (layout != CooperativeVectorMatrixLayout::InferencingOptimal &&
                        layout != CooperativeVectorMatrixLayout::TrainingOptimal)
                    {
                        CHECK_EQ(size, expectedSize);
                    }
                    else
                    {
                        // Optimal layouts are implementation defined!
                        CHECK_GT(size, 0);
                    }
                }
            }
        }
    }

    // Additional checks with specific rowColumnStride values.
    CHECK_EQ(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor, 16),
        128
    );
    CHECK_EQ(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor, 16),
        128
    );
    CHECK_EQ(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor, 32),
        240
    );
    CHECK_EQ(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor, 32),
        240
    );
}

/// Configuration for matrix conversion tests.
struct MatrixConvertTestConfig
{
    uint32_t rows;
    uint32_t cols;
    CooperativeVectorMatrixLayout srcLayout;
    CooperativeVectorMatrixLayout dstLayout;
    CooperativeVectorComponentType srcComponentType;
    CooperativeVectorComponentType dstComponentType;
    uint32_t srcStride; // 0 for tight packing
    uint32_t dstStride; // 0 for tight packing
};

/// Compute tight stride for a given layout (in elements, not bytes).
inline uint32_t getTightStride(uint32_t rows, uint32_t cols, CooperativeVectorMatrixLayout layout)
{
    return (layout == CooperativeVectorMatrixLayout::RowMajor) ? cols : rows;
}

/// Write a float value to a buffer at the given index using the specified component type.
inline void writeComponent(void* data, size_t index, float value, CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Float32:
        static_cast<float*>(data)[index] = value;
        break;
    case CooperativeVectorComponentType::Float16:
        static_cast<uint16_t*>(data)[index] = math::floatToHalf(value);
        break;
    default:
        FAIL("Unsupported component type for write");
    }
}

/// Read a float value from a buffer at the given index using the specified component type.
inline float readComponent(const void* data, size_t index, CooperativeVectorComponentType type)
{
    switch (type)
    {
    case CooperativeVectorComponentType::Float32:
        return static_cast<const float*>(data)[index];
    case CooperativeVectorComponentType::Float16:
        return math::halfToFloat(static_cast<const uint16_t*>(data)[index]);
    default:
        FAIL("Unsupported component type for read");
        return 0.f;
    }
}

/// Get the index into a linear buffer for a row-major or column-major matrix.
inline size_t getMatrixIndex(size_t r, size_t c, size_t rows, size_t cols, size_t stride, bool rowMajor)
{
    if (rowMajor)
        return r * stride + c;
    else
        return c * stride + r;
}

/// Compute buffer size in bytes for a matrix.
inline size_t getMatrixSizeBytes(
    uint32_t rows,
    uint32_t cols,
    uint32_t stride,
    bool rowMajor,
    CooperativeVectorComponentType componentType
)
{
    size_t componentSize = getCooperativeVectorComponentSize(componentType);
    return stride * (rowMajor ? rows : cols) * componentSize;
}

/// Compare two matrices with tolerance for floating point conversions.
inline bool compareMatrices(
    const void* srcData,
    uint32_t srcRows,
    uint32_t srcCols,
    uint32_t srcStride,
    bool srcRowMajor,
    CooperativeVectorComponentType srcType,
    const void* dstData,
    uint32_t dstRows,
    uint32_t dstCols,
    uint32_t dstStride,
    bool dstRowMajor,
    CooperativeVectorComponentType dstType,
    float tolerance = 0.f
)
{
    if (srcRows != dstRows || srcCols != dstCols)
        return false;

    for (size_t r = 0; r < srcRows; r++)
    {
        for (size_t c = 0; c < srcCols; c++)
        {
            size_t srcIdx = getMatrixIndex(r, c, srcRows, srcCols, srcStride, srcRowMajor);
            size_t dstIdx = getMatrixIndex(r, c, dstRows, dstCols, dstStride, dstRowMajor);

            float srcVal = readComponent(srcData, srcIdx, srcType);
            float dstVal = readComponent(dstData, dstIdx, dstType);

            if (std::abs(srcVal - dstVal) > tolerance)
                return false;
        }
    }
    return true;
}

/// Test matrix conversion on host.
inline void testMatrixConvertHost(IDevice* device, const MatrixConvertTestConfig& config)
{
    uint32_t srcStride =
        config.srcStride != 0 ? config.srcStride : getTightStride(config.rows, config.cols, config.srcLayout);
    uint32_t dstStride =
        config.dstStride != 0 ? config.dstStride : getTightStride(config.rows, config.cols, config.dstLayout);
    bool srcRowMajor = config.srcLayout == CooperativeVectorMatrixLayout::RowMajor;
    bool dstRowMajor = config.dstLayout == CooperativeVectorMatrixLayout::RowMajor;

    size_t srcComponentSize = getCooperativeVectorComponentSize(config.srcComponentType);
    size_t dstComponentSize = getCooperativeVectorComponentSize(config.dstComponentType);

    size_t srcSizeBytes = getMatrixSizeBytes(config.rows, config.cols, srcStride, srcRowMajor, config.srcComponentType);
    size_t dstSizeBytes = getMatrixSizeBytes(config.rows, config.cols, dstStride, dstRowMajor, config.dstComponentType);

    std::vector<uint8_t> srcData(srcSizeBytes);
    std::vector<uint8_t> dstData(dstSizeBytes);

    // Fill source matrix with test data.
    // Use small integer values that can be represented exactly in Float16.
    for (size_t r = 0; r < config.rows; r++)
    {
        for (size_t c = 0; c < config.cols; c++)
        {
            size_t idx = getMatrixIndex(r, c, config.rows, config.cols, srcStride, srcRowMajor);
            float value = (float)((r * config.cols + c) % 32);
            writeComponent(srcData.data(), idx, value, config.srcComponentType);
        }
    }

    CooperativeVectorMatrixDesc srcDesc = {};
    srcDesc.rowCount = config.rows;
    srcDesc.colCount = config.cols;
    srcDesc.componentType = config.srcComponentType;
    srcDesc.layout = config.srcLayout;
    srcDesc.size = srcSizeBytes;
    srcDesc.offset = 0;
    srcDesc.rowColumnStride = srcStride * srcComponentSize;

    CooperativeVectorMatrixDesc dstDesc = {};
    dstDesc.rowCount = config.rows;
    dstDesc.colCount = config.cols;
    dstDesc.componentType = config.dstComponentType;
    dstDesc.layout = config.dstLayout;
    dstDesc.size = dstSizeBytes;
    dstDesc.offset = 0;
    dstDesc.rowColumnStride = dstStride * dstComponentSize;

    REQUIRE_CALL(device->convertCooperativeVectorMatrix(
        dstData.data(),
        dstData.size(),
        &dstDesc,
        srcData.data(),
        srcData.size(),
        &srcDesc,
        1
    ));

    bool match = compareMatrices(
        srcData.data(),
        config.rows,
        config.cols,
        srcStride,
        srcRowMajor,
        config.srcComponentType,
        dstData.data(),
        config.rows,
        config.cols,
        dstStride,
        dstRowMajor,
        config.dstComponentType
    );
    CHECK(match);
}

/// Test matrix conversion on device.
inline void testMatrixConvertDevice(IDevice* device, const MatrixConvertTestConfig& config)
{
    uint32_t srcStride =
        config.srcStride != 0 ? config.srcStride : getTightStride(config.rows, config.cols, config.srcLayout);
    uint32_t dstStride =
        config.dstStride != 0 ? config.dstStride : getTightStride(config.rows, config.cols, config.dstLayout);
    bool srcRowMajor = config.srcLayout == CooperativeVectorMatrixLayout::RowMajor;
    bool dstRowMajor = config.dstLayout == CooperativeVectorMatrixLayout::RowMajor;

    size_t srcComponentSize = getCooperativeVectorComponentSize(config.srcComponentType);
    size_t dstComponentSize = getCooperativeVectorComponentSize(config.dstComponentType);

    size_t srcSizeBytes = getMatrixSizeBytes(config.rows, config.cols, srcStride, srcRowMajor, config.srcComponentType);
    size_t dstSizeBytes = getMatrixSizeBytes(config.rows, config.cols, dstStride, dstRowMajor, config.dstComponentType);

    std::vector<uint8_t> srcData(srcSizeBytes);
    std::vector<uint8_t> dstData(dstSizeBytes);

    // Fill source matrix with test data.
    // Use small integer values that can be represented exactly in Float16.
    for (size_t r = 0; r < config.rows; r++)
    {
        for (size_t c = 0; c < config.cols; c++)
        {
            size_t idx = getMatrixIndex(r, c, config.rows, config.cols, srcStride, srcRowMajor);
            float value = (float)((r * config.cols + c) % 32);
            writeComponent(srcData.data(), idx, value, config.srcComponentType);
        }
    }

    BufferDesc srcBufferDesc = {};
    srcBufferDesc.size = srcData.size();
    srcBufferDesc.memoryType = MemoryType::DeviceLocal;
    srcBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> srcBuffer;
    REQUIRE_CALL(device->createBuffer(srcBufferDesc, srcData.data(), srcBuffer.writeRef()));

    BufferDesc dstBufferDesc = {};
    dstBufferDesc.size = dstData.size();
    dstBufferDesc.memoryType = MemoryType::DeviceLocal;
    dstBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> dstBuffer;
    REQUIRE_CALL(device->createBuffer(dstBufferDesc, nullptr, dstBuffer.writeRef()));

    CooperativeVectorMatrixDesc srcDesc = {};
    srcDesc.rowCount = config.rows;
    srcDesc.colCount = config.cols;
    srcDesc.componentType = config.srcComponentType;
    srcDesc.layout = config.srcLayout;
    srcDesc.size = srcSizeBytes;
    srcDesc.offset = 0;
    srcDesc.rowColumnStride = srcStride * srcComponentSize;

    CooperativeVectorMatrixDesc dstDesc = {};
    dstDesc.rowCount = config.rows;
    dstDesc.colCount = config.cols;
    dstDesc.componentType = config.dstComponentType;
    dstDesc.layout = config.dstLayout;
    dstDesc.size = dstSizeBytes;
    dstDesc.offset = 0;
    dstDesc.rowColumnStride = dstStride * dstComponentSize;

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->convertCooperativeVectorMatrix(dstBuffer, &dstDesc, srcBuffer, &srcDesc, 1);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    REQUIRE_CALL(device->readBuffer(dstBuffer, 0, dstData.size(), dstData.data()));

    bool match = compareMatrices(
        srcData.data(),
        config.rows,
        config.cols,
        srcStride,
        srcRowMajor,
        config.srcComponentType,
        dstData.data(),
        config.rows,
        config.cols,
        dstStride,
        dstRowMajor,
        config.dstComponentType
    );
    CHECK(match);
}

/// Get test configurations for matrix conversion tests.
inline std::vector<MatrixConvertTestConfig> getMatrixConvertTestConfigs()
{
    std::vector<MatrixConvertTestConfig> configs;

    // Test various matrix sizes with tight packing and Float32.
    std::vector<std::pair<uint32_t, uint32_t>> sizes = {
        {4, 8},   // even x even
        {8, 4},   // even x even (transposed)
        {3, 7},   // odd x odd
        {7, 3},   // odd x odd (transposed)
        {5, 8},   // odd x even
        {8, 5},   // even x odd
        {1, 16},  // single row
        {16, 1},  // single column
        {16, 16}, // square
        {17, 17}, // odd square
    };

    std::vector<std::pair<CooperativeVectorMatrixLayout, CooperativeVectorMatrixLayout>> layoutPairs = {
        {CooperativeVectorMatrixLayout::RowMajor, CooperativeVectorMatrixLayout::ColumnMajor},
        {CooperativeVectorMatrixLayout::ColumnMajor, CooperativeVectorMatrixLayout::RowMajor},
        {CooperativeVectorMatrixLayout::RowMajor, CooperativeVectorMatrixLayout::RowMajor},
        {CooperativeVectorMatrixLayout::ColumnMajor, CooperativeVectorMatrixLayout::ColumnMajor},
    };

    // Test matrix sizes and layouts with Float32 -> Float32.
    for (const auto& [rows, cols] : sizes)
    {
        for (const auto& [srcLayout, dstLayout] : layoutPairs)
        {
            configs.push_back({
                rows,
                cols,
                srcLayout,
                dstLayout,
                CooperativeVectorComponentType::Float32,
                CooperativeVectorComponentType::Float32,
                0,
                0,
            });
        }
    }

    // Test with custom strides (larger than tight packing).
    // Use 8x8 matrix with extra padding.
    configs.push_back({
        8,
        8,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float32,
        16,
        16,
    });
    configs.push_back({
        8,
        8,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float32,
        16,
        16,
    });
    // Mixed: tight source, padded destination.
    configs.push_back({
        8,
        8,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float32,
        0,
        16,
    });
    // Mixed: padded source, tight destination.
    configs.push_back({
        8,
        8,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float32,
        16,
        0,
    });

    // Test component type conversions with a representative matrix size.
    std::vector<std::pair<CooperativeVectorComponentType, CooperativeVectorComponentType>> typePairs = {
        // Float16 conversions
        {CooperativeVectorComponentType::Float16, CooperativeVectorComponentType::Float16},
        {CooperativeVectorComponentType::Float32, CooperativeVectorComponentType::Float16},
        {CooperativeVectorComponentType::Float16, CooperativeVectorComponentType::Float32},
    };

    for (const auto& [srcType, dstType] : typePairs)
    {
        for (const auto& [srcLayout, dstLayout] : layoutPairs)
        {
            configs.push_back({
                8,
                8,
                srcLayout,
                dstLayout,
                srcType,
                dstType,
                0,
                0,
            });
        }
    }

    // Test component type conversion with odd-sized matrix.
    configs.push_back({
        7,
        5,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float16,
        0,
        0,
    });
    configs.push_back({
        7,
        5,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float16,
        CooperativeVectorComponentType::Float32,
        0,
        0,
    });

    // Test component type conversion with custom strides.
    configs.push_back({
        8,
        8,
        CooperativeVectorMatrixLayout::RowMajor,
        CooperativeVectorMatrixLayout::ColumnMajor,
        CooperativeVectorComponentType::Float32,
        CooperativeVectorComponentType::Float16,
        16,
        16,
    });

    return configs;
}

GPU_TEST_CASE("cooperative-vector-convert-matrix-host", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    for (const auto& config : getMatrixConvertTestConfigs())
    {
        CAPTURE(config.rows);
        CAPTURE(config.cols);
        CAPTURE(config.srcLayout);
        CAPTURE(config.dstLayout);
        CAPTURE(config.srcComponentType);
        CAPTURE(config.dstComponentType);
        CAPTURE(config.srcStride);
        CAPTURE(config.dstStride);
        testMatrixConvertHost(device, config);
    }
};

GPU_TEST_CASE("cooperative-vector-convert-matrix-device", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    for (const auto& config : getMatrixConvertTestConfigs())
    {
        CAPTURE(config.rows);
        CAPTURE(config.cols);
        CAPTURE(config.srcLayout);
        CAPTURE(config.dstLayout);
        CAPTURE(config.srcComponentType);
        CAPTURE(config.dstComponentType);
        CAPTURE(config.srcStride);
        CAPTURE(config.dstStride);
        testMatrixConvertDevice(device, config);
    }
};

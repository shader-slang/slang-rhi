#include "testing.h"

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
    size_t stride = rowColumnStride;
    if (stride == 0)
    {
        stride = getTightRowColumnStride(rowCount, colCount, componentType, layout);
    }

    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return stride * rowCount;
    case CooperativeVectorMatrixLayout::ColumnMajor:
        return stride * colCount;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
        return stride * colCount;
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return stride * colCount;
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

    // OptiX API automatically rounds up the matrix size to a multiple of 64 bytes.
    // This is different from the NVAPI and Vulkan API behavior.
    // We should consider changing the OptiX behavior to match the others for consistency.
    // For now, adjust the expected size for CUDA tests.
    bool isCUDA = device->getDeviceType() == DeviceType::CUDA;
    auto padSizeForCUDA = [&](size_t size)
    {
        return isCUDA ? (size + 63) & ~size_t(63) : size;
    };

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
                    size_t expectedSize = padSizeForCUDA(computeExpectedSize(rows, cols, type, layout));
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
        padSizeForCUDA(240)
    );
    CHECK_EQ(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor, 32),
        padSizeForCUDA(240)
    );
}

template<typename T>
class matrix_view
{
public:
    matrix_view(void* data, size_t rows, size_t cols, bool rowMajor, size_t stride = 0)
        : m_data(static_cast<T*>(data))
        , m_rows(rows)
        , m_cols(cols)
        , m_rowMajor(rowMajor)
        , m_stride(stride != 0 ? stride : (rowMajor ? cols : rows))
    {
    }

    size_t rows() const { return m_rows; }
    size_t cols() const { return m_cols; }
    bool rowMajor() const { return m_rowMajor; }
    size_t stride() const { return m_stride; }
    size_t strideBytes() const { return m_stride * sizeof(T); }
    size_t sizeBytes() const { return m_stride * (m_rowMajor ? m_rows : m_cols) * sizeof(T); }

    T& operator()(size_t r, size_t c) { return m_data[getIndex(r, c)]; }
    const T& operator()(size_t r, size_t c) const { return m_data[getIndex(r, c)]; }

    bool operator==(const matrix_view<T>& other) const
    {
        if (m_rows != other.m_rows || m_cols != other.m_cols)
            return false;
        for (size_t r = 0; r < m_rows; r++)
            for (size_t c = 0; c < m_cols; c++)
                if (operator()(r, c) != other(r, c))
                    return false;
        return true;
    }

private:
    size_t getIndex(size_t r, size_t c) const
    {
        if (m_rowMajor)
            return r * m_stride + c;
        else
            return c * m_stride + r;
    }

    T* m_data;
    size_t m_rows;
    size_t m_cols;
    bool m_rowMajor;
    size_t m_stride;
};

/// Configuration for matrix conversion tests.
struct MatrixConvertTestConfig
{
    uint32_t rows;
    uint32_t cols;
    CooperativeVectorMatrixLayout srcLayout;
    CooperativeVectorMatrixLayout dstLayout;
    uint32_t srcStride; // 0 for tight packing
    uint32_t dstStride; // 0 for tight packing
};

/// Compute tight stride for a given layout (in elements, not bytes).
inline uint32_t getTightStride(uint32_t rows, uint32_t cols, CooperativeVectorMatrixLayout layout)
{
    return (layout == CooperativeVectorMatrixLayout::RowMajor) ? cols : rows;
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

    matrix_view<float> srcView(nullptr, config.rows, config.cols, srcRowMajor, srcStride);
    matrix_view<float> dstView(nullptr, config.rows, config.cols, dstRowMajor, dstStride);

    std::vector<uint8_t> srcData(srcView.sizeBytes());
    std::vector<uint8_t> dstData(dstView.sizeBytes());

    matrix_view<float> srcMatrix(srcData.data(), config.rows, config.cols, srcRowMajor, srcStride);
    for (size_t r = 0; r < config.rows; r++)
        for (size_t c = 0; c < config.cols; c++)
            srcMatrix(r, c) = (float)(r * config.cols + c);

    CooperativeVectorMatrixDesc srcDesc = {};
    srcDesc.rowCount = config.rows;
    srcDesc.colCount = config.cols;
    srcDesc.componentType = CooperativeVectorComponentType::Float32;
    srcDesc.layout = config.srcLayout;
    srcDesc.size = srcView.sizeBytes();
    srcDesc.offset = 0;
    srcDesc.rowColumnStride = srcStride * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc = {};
    dstDesc.rowCount = config.rows;
    dstDesc.colCount = config.cols;
    dstDesc.componentType = CooperativeVectorComponentType::Float32;
    dstDesc.layout = config.dstLayout;
    dstDesc.size = dstView.sizeBytes();
    dstDesc.offset = 0;
    dstDesc.rowColumnStride = dstStride * sizeof(float);

    REQUIRE_CALL(device->convertCooperativeVectorMatrix(
        dstData.data(),
        dstData.size(),
        &dstDesc,
        srcData.data(),
        srcData.size(),
        &srcDesc,
        1
    ));

    matrix_view<float> dstMatrix(dstData.data(), config.rows, config.cols, dstRowMajor, dstStride);
    CHECK(srcMatrix == dstMatrix);
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

    matrix_view<float> srcView(nullptr, config.rows, config.cols, srcRowMajor, srcStride);
    matrix_view<float> dstView(nullptr, config.rows, config.cols, dstRowMajor, dstStride);

    std::vector<uint8_t> srcData(srcView.sizeBytes());
    std::vector<uint8_t> dstData(dstView.sizeBytes());

    matrix_view<float> srcMatrix(srcData.data(), config.rows, config.cols, srcRowMajor, srcStride);
    for (size_t r = 0; r < config.rows; r++)
        for (size_t c = 0; c < config.cols; c++)
            srcMatrix(r, c) = (float)(r * config.cols + c);

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
    srcDesc.componentType = CooperativeVectorComponentType::Float32;
    srcDesc.layout = config.srcLayout;
    srcDesc.size = srcView.sizeBytes();
    srcDesc.offset = 0;
    srcDesc.rowColumnStride = srcStride * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc = {};
    dstDesc.rowCount = config.rows;
    dstDesc.colCount = config.cols;
    dstDesc.componentType = CooperativeVectorComponentType::Float32;
    dstDesc.layout = config.dstLayout;
    dstDesc.size = dstView.sizeBytes();
    dstDesc.offset = 0;
    dstDesc.rowColumnStride = dstStride * sizeof(float);

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->convertCooperativeVectorMatrix(dstBuffer, &dstDesc, srcBuffer, &srcDesc, 1);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    REQUIRE_CALL(device->readBuffer(dstBuffer, 0, dstData.size(), dstData.data()));

    matrix_view<float> dstMatrix(dstData.data(), config.rows, config.cols, dstRowMajor, dstStride);
    CHECK(srcMatrix == dstMatrix);
}

/// Get test configurations for matrix conversion tests.
inline std::vector<MatrixConvertTestConfig> getMatrixConvertTestConfigs()
{
    std::vector<MatrixConvertTestConfig> configs;

    // Test various matrix sizes with tight packing.
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

    for (const auto& [rows, cols] : sizes)
    {
        for (const auto& [srcLayout, dstLayout] : layoutPairs)
        {
            configs.push_back({rows, cols, srcLayout, dstLayout, 0, 0});
        }
    }

    // Test with custom strides (larger than tight packing).
    // Use 8x8 matrix with extra padding.
    configs.push_back(
        {8, 8, CooperativeVectorMatrixLayout::RowMajor, CooperativeVectorMatrixLayout::ColumnMajor, 16, 16}
    );
    configs.push_back(
        {8, 8, CooperativeVectorMatrixLayout::ColumnMajor, CooperativeVectorMatrixLayout::RowMajor, 16, 16}
    );
    // Mixed: tight source, padded destination.
    configs.push_back(
        {8, 8, CooperativeVectorMatrixLayout::RowMajor, CooperativeVectorMatrixLayout::ColumnMajor, 0, 16}
    );
    // Mixed: padded source, tight destination.
    configs.push_back(
        {8, 8, CooperativeVectorMatrixLayout::RowMajor, CooperativeVectorMatrixLayout::ColumnMajor, 16, 0}
    );

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
        CAPTURE(config.srcStride);
        CAPTURE(config.dstStride);
        testMatrixConvertDevice(device, config);
    }
};

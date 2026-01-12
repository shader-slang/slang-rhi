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
    matrix_view(void* data, size_t rows, size_t cols, bool rowMajor)
        : m_data(static_cast<T*>(data))
        , m_rows(rows)
        , m_cols(cols)
        , m_rowMajor(rowMajor)
    {
    }

    size_t rows() const { return m_rows; }
    size_t cols() const { return m_cols; }
    bool rowMajor() const { return m_rowMajor; }
    size_t sizeBytes() const { return m_rows * m_cols * sizeof(T); }

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
            return r * m_cols + c;
        else
            return c * m_rows + r;
    }

    T* m_data;
    size_t m_rows;
    size_t m_cols;
    bool m_rowMajor;
};

GPU_TEST_CASE("cooperative-vector-convert-matrix-host", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    constexpr size_t rows = 4;
    constexpr size_t cols = 8;
    constexpr size_t matrixSize = rows * cols * sizeof(float);

    uint8_t inputData[2 * matrixSize];
    uint8_t outputData[2 * matrixSize];

    matrix_view<float> inputMatrix1(inputData, rows, cols, true);
    for (size_t r = 0; r < rows; r++)
        for (size_t c = 0; c < cols; c++)
            inputMatrix1(r, c) = (float)(r * cols + c);

    matrix_view<float> inputMatrix2(inputData + matrixSize, rows, cols, true);
    for (size_t r = 0; r < rows; r++)
        for (size_t c = 0; c < cols; c++)
            inputMatrix2(r, c) = (float)(r * cols + c + 100);

    CooperativeVectorMatrixDesc srcDesc1 = {};
    srcDesc1.rowCount = rows;
    srcDesc1.colCount = cols;
    srcDesc1.componentType = CooperativeVectorComponentType::Float32;
    srcDesc1.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc1.size = matrixSize;
    srcDesc1.offset = 0;
    srcDesc1.rowColumnStride = srcDesc1.colCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDesc2 = {};
    srcDesc2.rowCount = rows;
    srcDesc2.colCount = cols;
    srcDesc2.componentType = CooperativeVectorComponentType::Float32;
    srcDesc2.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc2.size = matrixSize;
    srcDesc2.offset = matrixSize;
    srcDesc2.rowColumnStride = srcDesc2.colCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc1 = {};
    dstDesc1.rowCount = rows;
    dstDesc1.colCount = cols;
    dstDesc1.componentType = CooperativeVectorComponentType::Float32;
    dstDesc1.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc1.size = matrixSize;
    dstDesc1.offset = 0;
    dstDesc1.rowColumnStride = dstDesc1.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc2 = {};
    dstDesc2.rowCount = rows;
    dstDesc2.colCount = cols;
    dstDesc2.componentType = CooperativeVectorComponentType::Float32;
    dstDesc2.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc2.size = matrixSize;
    dstDesc2.offset = matrixSize;
    dstDesc2.rowColumnStride = dstDesc2.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDescs[] = {srcDesc1, srcDesc2};
    CooperativeVectorMatrixDesc dstDescs[] = {dstDesc1, dstDesc2};

    REQUIRE_CALL(device->convertCooperativeVectorMatrix(
        outputData,
        sizeof(outputData),
        dstDescs,
        inputData,
        sizeof(inputData),
        srcDescs,
        2
    ));

    matrix_view<float> outputMatrix1(outputData, rows, cols, false);
    CHECK(inputMatrix1 == outputMatrix1);
    matrix_view<float> outputMatrix2(outputData + matrixSize, rows, cols, false);
    CHECK(inputMatrix2 == outputMatrix2);
};

GPU_TEST_CASE("cooperative-vector-convert-matrix-device", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    constexpr size_t rows = 4;
    constexpr size_t cols = 8;
    constexpr size_t matrixSize = rows * cols * sizeof(float);

    uint8_t inputData[2 * matrixSize];
    uint8_t outputData[2 * matrixSize];

    matrix_view<float> inputMatrix1(inputData, rows, cols, true);
    for (size_t r = 0; r < rows; r++)
        for (size_t c = 0; c < cols; c++)
            inputMatrix1(r, c) = (float)(r * cols + c);

    matrix_view<float> inputMatrix2(inputData + matrixSize, rows, cols, true);
    for (size_t r = 0; r < rows; r++)
        for (size_t c = 0; c < cols; c++)
            inputMatrix2(r, c) = (float)(r * cols + c + 100);

    BufferDesc inputBufferDesc = {};
    inputBufferDesc.size = sizeof(inputData);
    inputBufferDesc.memoryType = MemoryType::DeviceLocal;
    inputBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> inputBuffer;
    REQUIRE_CALL(device->createBuffer(inputBufferDesc, inputData, inputBuffer.writeRef()));

    BufferDesc outputBufferDesc = {};
    outputBufferDesc.size = sizeof(outputData);
    outputBufferDesc.memoryType = MemoryType::DeviceLocal;
    outputBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> outputBuffer;
    REQUIRE_CALL(device->createBuffer(outputBufferDesc, nullptr, outputBuffer.writeRef()));

    CooperativeVectorMatrixDesc srcDesc1 = {};
    srcDesc1.rowCount = rows;
    srcDesc1.colCount = cols;
    srcDesc1.componentType = CooperativeVectorComponentType::Float32;
    srcDesc1.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc1.size = matrixSize;
    srcDesc1.offset = 0;
    srcDesc1.rowColumnStride = srcDesc1.colCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDesc2 = {};
    srcDesc2.rowCount = rows;
    srcDesc2.colCount = cols;
    srcDesc2.componentType = CooperativeVectorComponentType::Float32;
    srcDesc2.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc2.size = matrixSize;
    srcDesc2.offset = matrixSize;
    srcDesc2.rowColumnStride = srcDesc2.colCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc1 = {};
    dstDesc1.rowCount = rows;
    dstDesc1.colCount = cols;
    dstDesc1.componentType = CooperativeVectorComponentType::Float32;
    dstDesc1.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc1.size = matrixSize;
    dstDesc1.offset = 0;
    dstDesc1.rowColumnStride = dstDesc1.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc2 = {};
    dstDesc2.rowCount = rows;
    dstDesc2.colCount = cols;
    dstDesc2.componentType = CooperativeVectorComponentType::Float32;
    dstDesc2.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc2.size = matrixSize;
    dstDesc2.offset = matrixSize;
    dstDesc2.rowColumnStride = dstDesc2.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDescs[] = {srcDesc1, srcDesc2};
    CooperativeVectorMatrixDesc dstDescs[] = {dstDesc1, dstDesc2};

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->convertCooperativeVectorMatrix(outputBuffer, dstDescs, inputBuffer, srcDescs, 2);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    REQUIRE_CALL(device->readBuffer(outputBuffer, 0, sizeof(outputData), outputData));

    matrix_view<float> outputMatrix1(outputData, rows, cols, false);
    CHECK(inputMatrix1 == outputMatrix1);
    matrix_view<float> outputMatrix2(outputData + matrixSize, rows, cols, false);
    CHECK(inputMatrix2 == outputMatrix2);
};

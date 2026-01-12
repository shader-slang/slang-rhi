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

template<typename T, size_t Rows, size_t Cols, bool RowMajor>
class matrix_view
{
public:
    matrix_view(void* data)
        : m_data(static_cast<T*>(data))
    {
    }

    static constexpr size_t sizeBytes() { return Rows * Cols * sizeof(T); }

    T& operator()(size_t r, size_t c) { return m_data[getIndex(r, c)]; }
    const T& operator()(size_t r, size_t c) const { return m_data[getIndex(r, c)]; }

    template<bool RowMajorOther>
    bool operator==(const matrix_view<T, Rows, Cols, RowMajorOther>& other) const
    {
        for (size_t r = 0; r < Rows; r++)
            for (size_t c = 0; c < Cols; c++)
                if (operator()(r, c) != other(r, c))
                    return false;
        return true;
    }

private:
    size_t getIndex(size_t r, size_t c) const
    {
        if constexpr (RowMajor)
            return r * Cols + c;
        else
            return c * Rows + r;
    }

    T* m_data;
};

GPU_TEST_CASE("cooperative-vector-convert-matrix-host", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    using matrix_in = matrix_view<float, 4, 8, true>;
    using matrix_out = matrix_view<float, 4, 8, false>;

    uint8_t inputData[2 * 4 * 8 * sizeof(float)];
    uint8_t outputData[2 * 4 * 8 * sizeof(float)];

    matrix_in inputMatrix1(inputData);
    for (size_t r = 0; r < 4; r++)
        for (size_t c = 0; c < 8; c++)
            inputMatrix1(r, c) = (float)(r * 8 + c);

    matrix_in inputMatrix2(inputData + matrix_in::sizeBytes());
    for (size_t r = 0; r < 4; r++)
        for (size_t c = 0; c < 8; c++)
            inputMatrix2(r, c) = (float)(r * 8 + c + 100);

    CooperativeVectorMatrixDesc srcDesc1 = {};
    srcDesc1.rowCount = 4;
    srcDesc1.colCount = 8;
    srcDesc1.componentType = CooperativeVectorComponentType::Float32;
    srcDesc1.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc1.size = matrix_in::sizeBytes();
    srcDesc1.offset = 0;
    srcDesc1.rowColumnStride = srcDesc1.colCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDesc2 = {};
    srcDesc2.rowCount = 4;
    srcDesc2.colCount = 8;
    srcDesc2.componentType = CooperativeVectorComponentType::Float32;
    srcDesc2.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc2.size = matrix_in::sizeBytes();
    srcDesc2.offset = matrix_in::sizeBytes();
    srcDesc2.rowColumnStride = srcDesc2.colCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc1 = {};
    dstDesc1.rowCount = 4;
    dstDesc1.colCount = 8;
    dstDesc1.componentType = CooperativeVectorComponentType::Float32;
    dstDesc1.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc1.size = matrix_out::sizeBytes();
    dstDesc1.offset = 0;
    dstDesc1.rowColumnStride = dstDesc1.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc2 = {};
    dstDesc2.rowCount = 4;
    dstDesc2.colCount = 8;
    dstDesc2.componentType = CooperativeVectorComponentType::Float32;
    dstDesc2.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc2.size = matrix_out::sizeBytes();
    dstDesc2.offset = matrix_out::sizeBytes();
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

    matrix_out outputMatrix(outputData);
    CHECK(inputMatrix1 == outputMatrix);
    matrix_out outputMatrix2(outputData + matrix_out::sizeBytes());
    CHECK(inputMatrix2 == outputMatrix2);
};

GPU_TEST_CASE("cooperative-vector-convert-matrix-device", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::CooperativeVector))
        SKIP("cooperative vector not supported");

    using matrix_in = matrix_view<float, 4, 8, true>;
    using matrix_out = matrix_view<float, 4, 8, false>;

    uint8_t inputData[2 * 4 * 8 * sizeof(float)];
    uint8_t outputData[2 * 4 * 8 * sizeof(float)];

    matrix_in inputMatrix1(inputData);
    for (size_t r = 0; r < 4; r++)
        for (size_t c = 0; c < 8; c++)
            inputMatrix1(r, c) = (float)(r * 8 + c);

    matrix_in inputMatrix2(inputData + matrix_in::sizeBytes());
    for (size_t r = 0; r < 4; r++)
        for (size_t c = 0; c < 8; c++)
            inputMatrix2(r, c) = (float)(r * 8 + c + 100);

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
    srcDesc1.rowCount = 4;
    srcDesc1.colCount = 8;
    srcDesc1.componentType = CooperativeVectorComponentType::Float32;
    srcDesc1.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc1.size = matrix_in::sizeBytes();
    srcDesc1.offset = 0;
    srcDesc1.rowColumnStride = srcDesc1.colCount * sizeof(float);

    CooperativeVectorMatrixDesc srcDesc2 = {};
    srcDesc2.rowCount = 4;
    srcDesc2.colCount = 8;
    srcDesc2.componentType = CooperativeVectorComponentType::Float32;
    srcDesc2.layout = CooperativeVectorMatrixLayout::RowMajor;
    srcDesc2.size = matrix_in::sizeBytes();
    srcDesc2.offset = matrix_in::sizeBytes();
    srcDesc2.rowColumnStride = srcDesc2.colCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc1 = {};
    dstDesc1.rowCount = 4;
    dstDesc1.colCount = 8;
    dstDesc1.componentType = CooperativeVectorComponentType::Float32;
    dstDesc1.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc1.size = matrix_out::sizeBytes();
    dstDesc1.offset = 0;
    dstDesc1.rowColumnStride = dstDesc1.rowCount * sizeof(float);

    CooperativeVectorMatrixDesc dstDesc2 = {};
    dstDesc2.rowCount = 4;
    dstDesc2.colCount = 8;
    dstDesc2.componentType = CooperativeVectorComponentType::Float32;
    dstDesc2.layout = CooperativeVectorMatrixLayout::ColumnMajor;
    dstDesc2.size = matrix_out::sizeBytes();
    dstDesc2.offset = matrix_out::sizeBytes();
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

    matrix_out outputMatrix(outputData);
    CHECK(inputMatrix1 == outputMatrix);
    matrix_out outputMatrix2(outputData + matrix_out::sizeBytes());
    CHECK(inputMatrix2 == outputMatrix2);
};

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

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

    // These should get padded to 64 bytes
    CHECK(querySize(4, 4, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(4, 4, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor) == 128);
    CHECK(querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor) == 128);
    CHECK(querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor, 16) == 128);
    CHECK(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor, 16) == 128
    );
    CHECK(querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor, 32) == 256);
    CHECK(
        querySize(8, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor, 32) == 256
    );

    CHECK(querySize(4, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(4, 8, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(8, 4, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(8, 4, CooperativeVectorComponentType::Float16, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(4, 4, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(4, 4, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(4, 8, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::RowMajor) == 128);
    CHECK(querySize(4, 8, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::ColumnMajor) == 128);

    CHECK(querySize(8, 4, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::RowMajor) == 128);
    CHECK(querySize(8, 4, CooperativeVectorComponentType::Float32, CooperativeVectorMatrixLayout::ColumnMajor) == 128);
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

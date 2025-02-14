#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cooperative-vector-properties", D3D12 | Vulkan)
{
    if (!device->hasFeature("cooperative-vector"))
        SKIP("cooperative vector not supported");

    uint32_t propertyCount = 0;
    REQUIRE_CALL(device->getCooperativeVectorProperties(nullptr, &propertyCount));
    std::vector<CooperativeVectorProperties> properties(propertyCount);
    REQUIRE_CALL(device->getCooperativeVectorProperties(properties.data(), &propertyCount));

    CHECK(propertyCount > 0);
}

GPU_TEST_CASE("cooperative-vector-query-size", D3D12 | Vulkan)
{
    if (!device->hasFeature("cooperative-vector"))
        SKIP("cooperative vector not supported");

    auto getComponentSize = [&](CooperativeVectorComponentType type)
    {
        switch (type)
        {
        case CooperativeVectorComponentType::Float16:
            return 2;
        case CooperativeVectorComponentType::Float32:
            return 4;
        case CooperativeVectorComponentType::Float64:
            return 8;
        default:
            return 0;
        }
    };

    auto querySize =
        [&](CooperativeVectorComponentType type, uint32_t rows, uint32_t cols, CooperativeVectorMatrixLayout layout)
    {
        ConvertCooperativeVectorMatrixDesc desc = {};
        desc.srcComponentType = type;
        desc.dstComponentType = type;
        desc.rowCount = rows;
        desc.colCount = cols;
        desc.srcLayout = layout;
        desc.dstLayout = layout;
        size_t stride = getComponentSize(type) * (layout == CooperativeVectorMatrixLayout::RowMajor ? cols : rows);
        desc.srcStride = stride;
        desc.dstStride = stride;
        size_t dstSize = 0;
        desc.dstSize = &dstSize;
        REQUIRE_CALL(device->convertCooperativeVectorMatrix(&desc, 1));
        return dstSize;
    };

    CHECK(querySize(CooperativeVectorComponentType::Float16, 4, 4, CooperativeVectorMatrixLayout::RowMajor) == 32);
    CHECK(querySize(CooperativeVectorComponentType::Float16, 4, 4, CooperativeVectorMatrixLayout::ColumnMajor) == 32);

    CHECK(querySize(CooperativeVectorComponentType::Float16, 4, 8, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(CooperativeVectorComponentType::Float16, 4, 8, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(CooperativeVectorComponentType::Float16, 8, 4, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(CooperativeVectorComponentType::Float16, 8, 4, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(CooperativeVectorComponentType::Float32, 4, 4, CooperativeVectorMatrixLayout::RowMajor) == 64);
    CHECK(querySize(CooperativeVectorComponentType::Float32, 4, 4, CooperativeVectorMatrixLayout::ColumnMajor) == 64);

    CHECK(querySize(CooperativeVectorComponentType::Float32, 4, 8, CooperativeVectorMatrixLayout::RowMajor) == 128);
    CHECK(querySize(CooperativeVectorComponentType::Float32, 4, 8, CooperativeVectorMatrixLayout::ColumnMajor) == 128);

    CHECK(querySize(CooperativeVectorComponentType::Float32, 8, 4, CooperativeVectorMatrixLayout::RowMajor) == 128);
    CHECK(querySize(CooperativeVectorComponentType::Float32, 8, 4, CooperativeVectorMatrixLayout::ColumnMajor) == 128);
}

GPU_TEST_CASE("cooperative-vector-convert-host", D3D12 | Vulkan)
{
    if (!device->hasFeature("cooperative-vector"))
        SKIP("cooperative vector not supported");

    float matrix[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            matrix[r][c] = (float)(r * 8 + c);
    float transposeMatrix[8][4];

    ConvertCooperativeVectorMatrixDesc desc = {};
    desc.srcData.hostAddress = matrix;
    desc.dstData.hostAddress = transposeMatrix;
    desc.srcComponentType = CooperativeVectorComponentType::Float32;
    desc.dstComponentType = CooperativeVectorComponentType::Float32;
    desc.rowCount = 4;
    desc.colCount = 8;
    desc.srcLayout = CooperativeVectorMatrixLayout::RowMajor;
    desc.dstLayout = CooperativeVectorMatrixLayout::ColumnMajor;
    desc.srcStride = desc.colCount * sizeof(float);
    desc.dstStride = desc.rowCount * sizeof(float);
    desc.srcSize = desc.rowCount * desc.colCount * sizeof(float);
    size_t dstSize = desc.rowCount * desc.colCount * sizeof(float);
    desc.dstSize = &dstSize;
    REQUIRE_CALL(device->convertCooperativeVectorMatrix(&desc, 1));

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            CHECK(matrix[r][c] == transposeMatrix[c][r]);
};

GPU_TEST_CASE("cooperative-vector-convert-device", D3D12 | Vulkan)
{
    if (!device->hasFeature("cooperative-vector"))
        SKIP("cooperative vector not supported");

    float matrix[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            matrix[r][c] = (float)(r * 8 + c);

    BufferDesc matrixBufferDesc = {};
    matrixBufferDesc.size = sizeof(matrix);
    matrixBufferDesc.memoryType = MemoryType::DeviceLocal;
    matrixBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
    ComPtr<IBuffer> matrixBuffer;
    REQUIRE_CALL(device->createBuffer(matrixBufferDesc, matrix, matrixBuffer.writeRef()));

    BufferDesc transposeMatrixBufferDesc = {};
    transposeMatrixBufferDesc.size = sizeof(matrix);
    transposeMatrixBufferDesc.memoryType = MemoryType::DeviceLocal;
    transposeMatrixBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> transposeMatrixBuffer;
    REQUIRE_CALL(device->createBuffer(transposeMatrixBufferDesc, nullptr, transposeMatrixBuffer.writeRef()));

    ConvertCooperativeVectorMatrixDesc desc = {};
    desc.srcData.deviceAddress = matrixBuffer->getDeviceAddress();
    desc.dstData.deviceAddress = transposeMatrixBuffer->getDeviceAddress();
    desc.srcComponentType = CooperativeVectorComponentType::Float32;
    desc.dstComponentType = CooperativeVectorComponentType::Float32;
    desc.rowCount = 4;
    desc.colCount = 8;
    desc.srcLayout = CooperativeVectorMatrixLayout::RowMajor;
    desc.dstLayout = CooperativeVectorMatrixLayout::ColumnMajor;
    desc.srcStride = desc.colCount * sizeof(float);
    desc.dstStride = desc.rowCount * sizeof(float);
    desc.srcSize = desc.rowCount * desc.colCount * sizeof(float);
    size_t dstSize = desc.rowCount * desc.colCount * sizeof(float);
    desc.dstSize = &dstSize;

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->convertCooperativeVectorMatrix(&desc, 1);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    float transposeMatrix[8][4];
    {
        ComPtr<ISlangBlob> data;
        REQUIRE_CALL(device->readBuffer(transposeMatrixBuffer, 0, sizeof(transposeMatrix), data.writeRef()));
        memcpy(transposeMatrix, data->getBufferPointer(), sizeof(transposeMatrix));
    }

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            CHECK(matrix[r][c] == transposeMatrix[c][r]);
};

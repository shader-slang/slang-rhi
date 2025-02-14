#include "testing.h"

#include "texture-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct TextureToTextureCopyInfo
{
    SubresourceRange srcSubresource;
    SubresourceRange dstSubresource;
    Extents extent;
    Offset3D srcOffset;
    Offset3D dstOffset;
};

struct TextureToBufferCopyInfo
{
    SubresourceRange srcSubresource;
    Extents extent;
    Offset3D textureOffset;
    Offset bufferOffset;
    Offset bufferSize;
};

struct BaseCopyTextureTest
{
    IDevice* device;

    Size alignedRowStride;

    RefPtr<TextureInfo> srcTextureInfo;
    RefPtr<TextureInfo> dstTextureInfo;
    TextureToTextureCopyInfo texCopyInfo;
    TextureToBufferCopyInfo bufferCopyInfo;

    ComPtr<ITexture> srcTexture;
    ComPtr<ITexture> dstTexture;
    ComPtr<IBuffer> resultsBuffer;

    RefPtr<ValidationTextureFormatBase> validationFormat;

    void init(IDevice* device, Format format, RefPtr<ValidationTextureFormatBase> validationFormat, TextureType type)
    {
        this->device = device;
        this->validationFormat = validationFormat;

        this->srcTextureInfo = new TextureInfo();
        this->srcTextureInfo->format = format;
        this->srcTextureInfo->textureType = type;

        this->dstTextureInfo = new TextureInfo();
        this->dstTextureInfo->format = format;
        this->dstTextureInfo->textureType = type;
    }

    void createRequiredResources()
    {
        TextureDesc srcTexDesc = {};
        srcTexDesc.type = srcTextureInfo->textureType;
        srcTexDesc.mipLevelCount = srcTextureInfo->mipLevelCount;
        srcTexDesc.arrayLength = srcTextureInfo->arrayLayerCount;
        srcTexDesc.size = srcTextureInfo->extents;
        srcTexDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource;
        if (srcTextureInfo->format == Format::D32_FLOAT || srcTextureInfo->format == Format::D16_UNORM)
        {
            srcTexDesc.usage |= (TextureUsage::DepthWrite | TextureUsage::DepthRead);
        }
        srcTexDesc.defaultState = ResourceState::ShaderResource;
        srcTexDesc.format = srcTextureInfo->format;

        REQUIRE_CALL(device->createTexture(srcTexDesc, srcTextureInfo->subresourceDatas.data(), srcTexture.writeRef()));

        TextureDesc dstTexDesc = {};
        dstTexDesc.type = dstTextureInfo->textureType;
        dstTexDesc.mipLevelCount = dstTextureInfo->mipLevelCount;
        dstTexDesc.arrayLength = dstTextureInfo->arrayLayerCount;
        dstTexDesc.size = dstTextureInfo->extents;
        dstTexDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination | TextureUsage::CopySource;
        if (dstTextureInfo->format == Format::D32_FLOAT || dstTextureInfo->format == Format::D16_UNORM)
        {
            dstTexDesc.usage |= (TextureUsage::DepthWrite | TextureUsage::DepthRead);
        }
        dstTexDesc.defaultState = ResourceState::CopyDestination;
        dstTexDesc.format = dstTextureInfo->format;

        REQUIRE_CALL(device->createTexture(dstTexDesc, dstTextureInfo->subresourceDatas.data(), dstTexture.writeRef()));

        auto bufferCopyExtents = bufferCopyInfo.extent;
        auto texelSize = getTexelSize(dstTextureInfo->format);
        size_t alignment;
        device->getTextureRowAlignment(&alignment);
        alignedRowStride = (bufferCopyExtents.width * texelSize + alignment - 1) & ~(alignment - 1);
        BufferDesc bufferDesc = {};
        bufferDesc.size = bufferCopyExtents.height * bufferCopyExtents.depth * alignedRowStride;
        bufferDesc.format = Format::Unknown;
        bufferDesc.elementSize = 0;
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                           BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::CopyDestination;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultsBuffer.writeRef()));

        bufferCopyInfo.bufferSize = bufferDesc.size;
    }

    void submitGPUWork()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        commandEncoder->copyTexture(
            dstTexture,
            texCopyInfo.dstSubresource,
            texCopyInfo.dstOffset,
            srcTexture,
            texCopyInfo.srcSubresource,
            texCopyInfo.srcOffset,
            texCopyInfo.extent
        );

        commandEncoder->copyTextureToBuffer(
            resultsBuffer,
            bufferCopyInfo.bufferOffset,
            bufferCopyInfo.bufferSize,
            alignedRowStride,
            dstTexture,
            bufferCopyInfo.srcSubresource,
            bufferCopyInfo.textureOffset,
            bufferCopyInfo.extent
        );

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    bool isWithinCopyBounds(uint32_t x, uint32_t y, uint32_t z)
    {
        auto copyExtents = texCopyInfo.extent;
        auto copyOffset = texCopyInfo.dstOffset;

        auto xLowerBound = copyOffset.x;
        auto xUpperBound = copyOffset.x + copyExtents.width;
        auto yLowerBound = copyOffset.y;
        auto yUpperBound = copyOffset.y + copyExtents.height;
        auto zLowerBound = copyOffset.z;
        auto zUpperBound = copyOffset.z + copyExtents.depth;

        if (x < xLowerBound || x >= xUpperBound || y < yLowerBound || y >= yUpperBound || z < zLowerBound ||
            z >= zUpperBound)
            return false;
        else
            return true;
    }

    void validateTestResults(
        ValidationTextureData actual,
        ValidationTextureData expectedCopied,
        ValidationTextureData expectedOriginal
    )
    {
        auto actualExtents = actual.extents;
        auto copyExtent = texCopyInfo.extent;
        auto srcTexOffset = texCopyInfo.srcOffset;
        auto dstTexOffset = texCopyInfo.dstOffset;

        for (uint32_t x = 0; x < actualExtents.width; ++x)
        {
            for (uint32_t y = 0; y < actualExtents.height; ++y)
            {
                for (uint32_t z = 0; z < actualExtents.depth; ++z)
                {
                    auto actualBlock = actual.getBlockAt(x, y, z);
                    if (isWithinCopyBounds(x, y, z))
                    {
                        // Block is located within the bounds of the source texture
                        auto xSource = x + srcTexOffset.x - dstTexOffset.x;
                        auto ySource = y + srcTexOffset.y - dstTexOffset.y;
                        auto zSource = z + srcTexOffset.z - dstTexOffset.z;
                        auto expectedBlock = expectedCopied.getBlockAt(xSource, ySource, zSource);
                        validationFormat->validateBlocksEqual(actualBlock, expectedBlock);
                    }
                    else
                    {
                        // Block is located outside the bounds of the source texture and should be compared
                        // against known expected values for the destination texture.
                        auto expectedBlock = expectedOriginal.getBlockAt(x, y, z);
                        validationFormat->validateBlocksEqual(actualBlock, expectedBlock);
                    }
                }
            }
        }
    }

    void checkTestResults(Extents srcMipExtent, const void* expectedCopiedData, const void* expectedOriginalData)
    {
        ComPtr<ISlangBlob> resultBlob;
        REQUIRE_CALL(device->readBuffer(resultsBuffer, 0, bufferCopyInfo.bufferSize, resultBlob.writeRef()));
        auto results = resultBlob->getBufferPointer();

        ValidationTextureData actual;
        actual.extents = bufferCopyInfo.extent;
        actual.textureData = results;
        actual.strides.x = getTexelSize(dstTextureInfo->format);
        actual.strides.y = alignedRowStride;
        actual.strides.z = actual.extents.height * actual.strides.y;

        ValidationTextureData expectedCopied;
        expectedCopied.extents = srcMipExtent;
        expectedCopied.textureData = expectedCopiedData;
        expectedCopied.strides.x = getTexelSize(srcTextureInfo->format);
        expectedCopied.strides.y = expectedCopied.extents.width * expectedCopied.strides.x;
        expectedCopied.strides.z = expectedCopied.extents.height * expectedCopied.strides.y;

        ValidationTextureData expectedOriginal;
        if (expectedOriginalData)
        {
            expectedOriginal.extents = bufferCopyInfo.extent;
            expectedOriginal.textureData = expectedOriginalData;
            expectedOriginal.strides.x = getTexelSize(dstTextureInfo->format);
            expectedOriginal.strides.y = expectedOriginal.extents.width * expectedOriginal.strides.x;
            expectedOriginal.strides.z = expectedOriginal.extents.height * expectedOriginal.strides.y;
        }

        validateTestResults(actual, expectedCopied, expectedOriginal);
    }
};

struct SimpleCopyTexture : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 4;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        dstTextureInfo = srcTextureInfo;

        generateTextureData(srcTextureInfo, validationFormat);

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = srcTextureInfo->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto subresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        auto expectedData = srcTextureInfo->subresourceDatas[subresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedData.data, nullptr);
    }
};

struct CopyTextureSection : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 4;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = (textureType == TextureType::Texture3D) ? 1 : 2;

        dstTextureInfo = srcTextureInfo;

        generateTextureData(srcTextureInfo, validationFormat);

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = (textureType == TextureType::Texture3D) ? 0 : 1;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = srcTextureInfo->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto subresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedData = srcTextureInfo->subresourceDatas[subresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedData.data, nullptr);
    }
};

struct LargeSrcToSmallDst : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 8;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 8;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        generateTextureData(srcTextureInfo, validationFormat);

        dstTextureInfo->extents.width = 4;
        dstTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        dstTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        dstTextureInfo->mipLevelCount = 1;
        dstTextureInfo->arrayLayerCount = 1;

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = dstTextureInfo->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto subresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedData = srcTextureInfo->subresourceDatas[subresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedData.data, nullptr);
    }
};

struct SmallSrcToLargeDst : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 4;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        generateTextureData(srcTextureInfo, validationFormat);

        dstTextureInfo->extents.width = 8;
        dstTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 8;
        dstTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        dstTextureInfo->mipLevelCount = 1;
        dstTextureInfo->arrayLayerCount = 1;

        generateTextureData(dstTextureInfo, validationFormat);

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = srcTextureInfo->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto copiedSubresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedCopiedData = srcTextureInfo->subresourceDatas[copiedSubresourceIndex];
        auto originalSubresourceIndex =
            getSubresourceIndex(dstSubresource.mipLevel, dstTextureInfo->mipLevelCount, dstSubresource.baseArrayLayer);
        SubresourceData expectedOriginalData = dstTextureInfo->subresourceDatas[originalSubresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedCopiedData.data, expectedOriginalData.data);
    }
};

struct CopyBetweenMips : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 16;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 16;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 4;
        srcTextureInfo->arrayLayerCount = 1;

        generateTextureData(srcTextureInfo, validationFormat);

        dstTextureInfo->extents.width = 16;
        dstTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 16;
        dstTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        dstTextureInfo->mipLevelCount = 4;
        dstTextureInfo->arrayLayerCount = 1;

        generateTextureData(dstTextureInfo, validationFormat);

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 2;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 1;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        auto copiedSubresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        auto originalSubresourceIndex =
            getSubresourceIndex(dstSubresource.mipLevel, dstTextureInfo->mipLevelCount, dstSubresource.baseArrayLayer);

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = srcTextureInfo->subresourceObjects[copiedSubresourceIndex]->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->subresourceObjects[originalSubresourceIndex]->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        SubresourceData expectedCopiedData = srcTextureInfo->subresourceDatas[copiedSubresourceIndex];
        SubresourceData expectedOriginalData = dstTextureInfo->subresourceDatas[originalSubresourceIndex];
        auto srcMipExtent = srcTextureInfo->subresourceObjects[2]->extents;
        checkTestResults(srcMipExtent, expectedCopiedData.data, expectedOriginalData.data);
    }
};

struct CopyBetweenLayers : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 4;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = (textureType == TextureType::Texture3D) ? 1 : 2;

        generateTextureData(srcTextureInfo, validationFormat);
        dstTextureInfo = srcTextureInfo;

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = (textureType == TextureType::Texture3D) ? 0 : 1;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent = srcTextureInfo->extents;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {0, 0, 0};

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto copiedSubresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedCopiedData = srcTextureInfo->subresourceDatas[copiedSubresourceIndex];
        auto originalSubresourceIndex =
            getSubresourceIndex(dstSubresource.mipLevel, dstTextureInfo->mipLevelCount, dstSubresource.baseArrayLayer);
        SubresourceData expectedOriginalData = dstTextureInfo->subresourceDatas[originalSubresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedCopiedData.data, expectedOriginalData.data);
    }
};

struct CopyWithOffsets : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 8;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 8;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        generateTextureData(srcTextureInfo, validationFormat);

        dstTextureInfo->extents.width = 16;
        dstTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 16;
        dstTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 4 : 1;
        dstTextureInfo->mipLevelCount = 1;
        dstTextureInfo->arrayLayerCount = 1;

        generateTextureData(dstTextureInfo, validationFormat);

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent.width = 4;
        texCopyInfo.extent.height = 4;
        texCopyInfo.extent.depth = 1;
        texCopyInfo.srcOffset = {2, 2, 0};
        texCopyInfo.dstOffset = {4, 4, 0};

        if (textureType == TextureType::Texture1D)
        {
            texCopyInfo.extent.height = 1;
            texCopyInfo.srcOffset.y = 0;
            texCopyInfo.dstOffset.y = 0;
        }
        else if (textureType == TextureType::Texture3D)
        {
            texCopyInfo.extent.depth = srcTextureInfo->extents.depth;
            texCopyInfo.dstOffset.z = 1;
        }

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto copiedSubresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedCopiedData = srcTextureInfo->subresourceDatas[copiedSubresourceIndex];
        auto originalSubresourceIndex =
            getSubresourceIndex(dstSubresource.mipLevel, dstTextureInfo->mipLevelCount, dstSubresource.baseArrayLayer);
        SubresourceData expectedOriginalData = dstTextureInfo->subresourceDatas[originalSubresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedCopiedData.data, expectedOriginalData.data);
    }
};

struct CopySectionWithSetExtent : BaseCopyTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 8;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 8;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        generateTextureData(srcTextureInfo, validationFormat);
        dstTextureInfo = srcTextureInfo;

        SubresourceRange srcSubresource = {};
        srcSubresource.mipLevel = 0;
        srcSubresource.mipLevelCount = 1;
        srcSubresource.baseArrayLayer = 0;
        srcSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        texCopyInfo.srcSubresource = srcSubresource;
        texCopyInfo.dstSubresource = dstSubresource;
        texCopyInfo.extent.width = 4;
        texCopyInfo.extent.height = 4;
        texCopyInfo.extent.depth = 1;
        texCopyInfo.srcOffset = {0, 0, 0};
        texCopyInfo.dstOffset = {4, 4, 0};

        if (textureType == TextureType::Texture1D)
        {
            texCopyInfo.extent.height = 1;
            texCopyInfo.dstOffset.y = 0;
        }
        else if (textureType == TextureType::Texture3D)
        {
            texCopyInfo.extent.depth = srcTextureInfo->extents.depth;
        }

        bufferCopyInfo.srcSubresource = dstSubresource;
        bufferCopyInfo.extent = dstTextureInfo->extents;
        bufferCopyInfo.textureOffset = {0, 0, 0};
        bufferCopyInfo.bufferOffset = 0;

        createRequiredResources();
        submitGPUWork();

        auto copiedSubresourceIndex =
            getSubresourceIndex(srcSubresource.mipLevel, srcTextureInfo->mipLevelCount, srcSubresource.baseArrayLayer);
        SubresourceData expectedCopiedData = srcTextureInfo->subresourceDatas[copiedSubresourceIndex];
        auto originalSubresourceIndex =
            getSubresourceIndex(dstSubresource.mipLevel, dstTextureInfo->mipLevelCount, dstSubresource.baseArrayLayer);
        SubresourceData expectedOriginalData = dstTextureInfo->subresourceDatas[originalSubresourceIndex];
        checkTestResults(srcTextureInfo->extents, expectedCopiedData.data, expectedOriginalData.data);
    }
};

template<typename T>
void testCopyTexture(IDevice* device)
{
    // Skip Type::Unknown and Type::Buffer as well as Format::Unknown
    // TODO: Add support for TextureCube
    Format formats[] = {
        Format::R8G8B8A8_UNORM,
        Format::R16_FLOAT,
        Format::R16G16_FLOAT,
        Format::R10G10B10A2_UNORM,
        Format::B5G5R5A1_UNORM
    };
    for (uint32_t i = (uint32_t)(TextureType::Texture1D); i <= (uint32_t)TextureType::Texture3D; ++i)
    {
        for (auto format : formats)
        {
            FormatSupport formatSupport;
            device->getFormatSupport(format, &formatSupport);
            if (!is_set(formatSupport, FormatSupport::Texture))
                continue;
            auto type = (TextureType)i;
            auto validationFormat = getValidationTextureFormat(format);
            if (!validationFormat)
                continue;

            T test;
            test.init(device, format, validationFormat, type);
            test.run();
        }
    }
}

// Texture support is currently very limited for D3D11, Metal, CUDA and CPU

GPU_TEST_CASE("copy-texture-simple", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<SimpleCopyTexture>(device);
}

GPU_TEST_CASE("copy-texture-section", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<CopyTextureSection>(device);
}

GPU_TEST_CASE("copy-texture-large-to-small", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<LargeSrcToSmallDst>(device);
}

GPU_TEST_CASE("copy-texture-small-to-large", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<SmallSrcToLargeDst>(device);
}

// TODO Metal: no support for 1D mips
// TODO WGPU: no support for 1D mips
GPU_TEST_CASE("copy-texture-between-mips", D3D12 | Vulkan)
{
    testCopyTexture<CopyBetweenMips>(device);
}

// TODO WGPU: no support for layers
GPU_TEST_CASE("copy-texture-between-layers", D3D12 | Vulkan | Metal)
{
    testCopyTexture<CopyBetweenLayers>(device);
}

GPU_TEST_CASE("copy-texture-with-offsets", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<CopyWithOffsets>(device);
}

GPU_TEST_CASE("copy-texture-with-extent", D3D12 | Vulkan | Metal | WGPU)
{
    testCopyTexture<CopySectionWithSetExtent>(device);
}

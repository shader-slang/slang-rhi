#include "testing.h"

#include "texture-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct TextureReadInfo
{
    SubresourceRange srcSubresource = {0, 1, 0, 1};
};

struct BaseReadTextureTest
{
    IDevice* device;

    Size alignedRowStride;

    RefPtr<TextureInfo> srcTextureInfo;
    TextureReadInfo texReadInfo;

    ComPtr<ITexture> srcTexture;

    RefPtr<ValidationTextureFormatBase> validationFormat;

    void init(IDevice* device, Format format, RefPtr<ValidationTextureFormatBase> validationFormat, TextureType type)
    {
        this->device = device;
        this->validationFormat = validationFormat;

        this->srcTextureInfo = new TextureInfo();
        this->srcTextureInfo->format = format;
        this->srcTextureInfo->textureType = type;
    }

    void createRequiredResources()
    {
        TextureDesc srcTexDesc = {};
        srcTexDesc.type = srcTextureInfo->textureType;
        srcTexDesc.mipLevelCount = srcTextureInfo->mipLevelCount;
        srcTexDesc.arrayLength = srcTextureInfo->arrayLayerCount;
        srcTexDesc.size = srcTextureInfo->extents;
        srcTexDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource;
        srcTexDesc.defaultState = ResourceState::ShaderResource;
        srcTexDesc.format = srcTextureInfo->format;

        REQUIRE_CALL(device->createTexture(srcTexDesc, srcTextureInfo->subresourceDatas.data(), srcTexture.writeRef()));
    }

    void validateTestResults(ValidationTextureData actual, ValidationTextureData expectedCopied)
    {
        auto actualExtents = actual.extents;
        for (uint32_t x = 0; x < actualExtents.width; ++x)
        {
            for (uint32_t y = 0; y < actualExtents.height; ++y)
            {
                for (uint32_t z = 0; z < actualExtents.depth; ++z)
                {
                    auto actualBlock = actual.getBlockAt(x, y, z);
                    auto expectedBlock = expectedCopied.getBlockAt(x, y, z);
                    validationFormat->validateBlocksEqual(actualBlock, expectedBlock);
                }
            }
        }
    }

    void checkTestResults()
    {
        generateTextureData(srcTextureInfo, validationFormat);

        createRequiredResources();

        uint32_t subresourceIndex = getSubresourceIndex(
            texReadInfo.srcSubresource.mipLevel,
            srcTextureInfo->mipLevelCount,
            texReadInfo.srcSubresource.baseArrayLayer
        );

        SubresourceLayout layout;
        REQUIRE_CALL(srcTexture->getSubresourceLayout(texReadInfo.srcSubresource.mipLevel, &layout));

        SubresourceData subresourceData = srcTextureInfo->subresourceDatas[subresourceIndex];

        ValidationTextureData expectedCopied;
        expectedCopied.extents = layout.size;
        expectedCopied.textureData = subresourceData.data;
        expectedCopied.strides.x = getTexelSize(srcTextureInfo->format);
        expectedCopied.strides.y = subresourceData.strideY;
        expectedCopied.strides.z = subresourceData.strideZ;

        ComPtr<ISlangBlob> readBlob;
        REQUIRE_CALL(device->readTexture(
            srcTexture,
            texReadInfo.srcSubresource.baseArrayLayer,
            texReadInfo.srcSubresource.mipLevel,
            readBlob.writeRef(),
            nullptr,
            nullptr
        ));

        auto readResults = readBlob->getBufferPointer();

        ValidationTextureData actual = expectedCopied;
        actual.textureData = readResults;
        actual.strides.y = layout.strideY;
        actual.strides.z = layout.strideZ;

        validateTestResults(actual, expectedCopied);
    }
};

struct SimpleReadTexture : BaseReadTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;
        auto format = srcTextureInfo->format;

        srcTextureInfo->extents.width = 8;
        srcTextureInfo->extents.height = (textureType == TextureType::Texture1D) ? 1 : 4;
        srcTextureInfo->extents.depth = (textureType == TextureType::Texture3D) ? 2 : 1;
        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLayerCount = 1;

        checkTestResults();
    }
};

template<typename T>
void testReadTexture(IDevice* device)
{
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

GPU_TEST_CASE("read-texture-simple", D3D12 | Vulkan | Metal | WGPU)
{
    testReadTexture<SimpleReadTexture>(device);
}

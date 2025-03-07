#include "testing.h"

#include "texture-utils.h"

using namespace rhi;
using namespace rhi::testing;

int32_t heightFromWidth(TextureType type, int32_t width)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return 1; // 1D is always 1 high.
    case TextureType::Texture2D:
    case TextureType::Texture3D:
        return width / 2; // Height is half width (so not a square!)
    case TextureType::TextureCube:
        return width; // Cube map dimensions match
    default:
        return 0;
    }
}

struct BaseReadTextureTest
{
    IDevice* device;

    RefPtr<TextureInfo> srcTextureInfo;
    ComPtr<ITexture> srcTexture;

    RefPtr<ValidationTextureFormatBase> validationFormat;

    void init(
        IDevice* _device,
        Format _format,
        RefPtr<ValidationTextureFormatBase> _validationFormat,
        TextureType _type
    )
    {
        device = _device;
        validationFormat = _validationFormat;

        srcTextureInfo = new TextureInfo();
        srcTextureInfo->format = _format;
        srcTextureInfo->textureType = _type;
        srcTextureInfo->extents.width = 8;
        srcTextureInfo->extents.height = heightFromWidth(_type, srcTextureInfo->extents.width);
        srcTextureInfo->extents.depth = (_type == TextureType::Texture3D) ? 2 : 1;
    }

    void createRequiredResources()
    {
        TextureDesc srcTexDesc = {};
        srcTexDesc.type = srcTextureInfo->textureType;
        srcTexDesc.mipLevelCount = srcTextureInfo->mipLevelCount;
        srcTexDesc.arrayLength = srcTextureInfo->arrayLength;
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

        TextureDesc desc = srcTexture->getDesc();

        uint32_t layerCount = desc.getLayerCount();
        for (uint32_t layerIndex = 0; layerIndex < layerCount; layerIndex++)
        {
            for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; mipLevel++)
            {
                uint32_t subresourceIndex = getSubresourceIndex(mipLevel, desc.mipLevelCount, layerIndex);

                SubresourceLayout layout;
                REQUIRE_CALL(srcTexture->getSubresourceLayout(mipLevel, &layout));

                SubresourceData subresourceData = srcTextureInfo->subresourceDatas[subresourceIndex];

                ValidationTextureData expectedCopied;
                expectedCopied.extents = layout.size;
                expectedCopied.textureData = subresourceData.data;
                expectedCopied.strides.x = getTexelSize(desc.format);
                expectedCopied.strides.y = subresourceData.strideY;
                expectedCopied.strides.z = subresourceData.strideZ;

                ComPtr<ISlangBlob> readBlob;
                REQUIRE_CALL(
                    device->readTexture(srcTexture, layerIndex, mipLevel, readBlob.writeRef(), nullptr, nullptr)
                );

                auto readResults = readBlob->getBufferPointer();

                ValidationTextureData actual = expectedCopied;
                actual.textureData = readResults;
                actual.strides.y = layout.strideY;
                actual.strides.z = layout.strideZ;

                validateTestResults(actual, expectedCopied);
            }
        }
    }
};

struct SimpleReadTexture : BaseReadTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;

        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLength = 1;

        checkTestResults();
    }
};

struct ArrayReadTexture : BaseReadTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;

        if (textureType == TextureType::Texture3D)
            return;
        if (textureType == TextureType::Texture1D && device->getDeviceInfo().deviceType == DeviceType::WGPU)
            return;

        srcTextureInfo->mipLevelCount = 1;
        srcTextureInfo->arrayLength = 8;

        checkTestResults();
    }
};

struct MipsReadTexture : BaseReadTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;

        if (textureType == TextureType::Texture1D && device->getDeviceInfo().deviceType == DeviceType::WGPU)
            return;

        srcTextureInfo->mipLevelCount = 2;
        srcTextureInfo->arrayLength = 1;

        checkTestResults();
    }
};

struct ArrayMipsReadTexture : BaseReadTextureTest
{
    void run()
    {
        auto textureType = srcTextureInfo->textureType;

        if (textureType == TextureType::Texture3D)
            return;
        if (textureType == TextureType::Texture1D && device->getDeviceInfo().deviceType == DeviceType::WGPU)
            return;

        srcTextureInfo->mipLevelCount = 2;
        srcTextureInfo->arrayLength = 4;

        checkTestResults();
    }
};

template<typename T>
void testReadTexture(IDevice* device)
{
    Format formats[] = {
        Format::R8G8B8A8_UNORM,
        Format::R16_FLOAT,
        Format::R16G16_FLOAT,
        Format::R10G10B10A2_UNORM,
        Format::B5G5R5A1_UNORM,
        Format::R32G32B32A32_FLOAT
    };
    for (uint32_t i = (uint32_t)(TextureType::Texture1D); i <= (uint32_t)TextureType::TextureCube; ++i)
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

GPU_TEST_CASE("read-texture-simple", D3D12 | Vulkan | WGPU | Metal)
{
    testReadTexture<SimpleReadTexture>(device);
}
GPU_TEST_CASE("read-texture-array", D3D12 | Vulkan | WGPU | Metal)
{
    testReadTexture<ArrayReadTexture>(device);
}
GPU_TEST_CASE("read-texture-mips", D3D12 | Vulkan | WGPU | Metal)
{
    testReadTexture<MipsReadTexture>(device);
}
GPU_TEST_CASE("read-texture-arraymips", D3D12 | Vulkan | WGPU | Metal)
{
    testReadTexture<ArrayMipsReadTexture>(device);
}

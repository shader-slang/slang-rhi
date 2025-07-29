#include "metal-clear-engine.h"
#include "metal-texture.h"
#include "metal-utils.h"

#include "format-conversion.h"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(resources);

namespace rhi::metal {

Result ClearEngine::initialize(MTL::Device* device)
{
    auto fs = cmrc::resources::get_filesystem();
    auto shader = fs.open("src/metal/shaders/clear-texture.metal");

    auto source = createStringView((void*)shader.begin(), shader.size());

    NS::Error* error = nullptr;
    m_library = NS::TransferPtr(device->newLibrary(source.get(), nullptr, &error));
    if (error)
    {
        fprintf(stderr, "Metal error: %s\n", error->localizedDescription()->utf8String());
        return SLANG_FAIL;
    }

    for (size_t textureType = 0; textureType < kTextureTypeCount; ++textureType)
    {
        if (textureType == size_t(TextureType::Texture2DMS) || textureType == size_t(TextureType::Texture2DMSArray))
        {
            continue;
        }

        for (size_t type = 0; type < size_t(Type::Count); ++type)
        {
            static const char* textureTypeNames[] = {
                "texture1d",
                "texture1d_array",
                "texture2d",
                "texture2d_array",
                "texture2d",
                "texture2d_array",
                "texture3d",
                "texturecube",
                "texturecube_array",
            };
            static const char* typeNames[] = {"float", "half", "uint", "int"};
            SLANG_RHI_ASSERT(textureType < SLANG_COUNT_OF(textureTypeNames));
            SLANG_RHI_ASSERT(type < SLANG_COUNT_OF(typeNames));
            char name[128];
            snprintf(name, sizeof(name), "clear_%s_%s", textureTypeNames[textureType], typeNames[type]);
            auto functionName = createString(name);
            NS::SharedPtr<MTL::Function> function = NS::TransferPtr(m_library->newFunction(functionName.get()));
            if (!function)
            {
                return SLANG_FAIL;
            }
            NS::SharedPtr<MTL::ComputePipelineState> pipelineState =
                NS::TransferPtr(device->newComputePipelineState(function.get(), &error));
            if (error)
            {
                fprintf(stderr, "Metal error: %s\n", error->localizedDescription()->utf8String());
                return SLANG_FAIL;
            }
            m_clearPipelines[size_t(textureType)][size_t(type)] = pipelineState;
        }
    }

    m_threadGroupSizes[size_t(TextureType::Texture1D)] = MTL::Size{256, 1, 1};
    m_threadGroupSizes[size_t(TextureType::Texture1DArray)] = MTL::Size{256, 1, 1};
    m_threadGroupSizes[size_t(TextureType::Texture2D)] = MTL::Size{32, 32, 1};
    m_threadGroupSizes[size_t(TextureType::Texture2DArray)] = MTL::Size{32, 32, 1};
    m_threadGroupSizes[size_t(TextureType::Texture3D)] = MTL::Size{8, 8, 8};
    m_threadGroupSizes[size_t(TextureType::TextureCube)] = MTL::Size{32, 32, 1};
    m_threadGroupSizes[size_t(TextureType::TextureCubeArray)] = MTL::Size{32, 32, 1};

    return SLANG_OK;
}

void ClearEngine::release()
{
    for (size_t textureType = 0; textureType <= size_t(TextureType::TextureCubeArray); ++textureType)
    {
        for (size_t type = 0; type < size_t(Type::Count); ++type)
        {
            m_clearPipelines[textureType][type].reset();
        }
    }
    m_library.reset();
}

void ClearEngine::clearTextureUint(
    MTL::ComputeCommandEncoder* encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const uint32_t clearValue[4]
)
{
    Type type = getFormatInfo(texture->m_desc.format).isSigned ? Type::Int : Type::Uint;
    clearTexture(encoder, texture, subresourceRange, type, clearValue, sizeof(uint32_t[4]));
}

void ClearEngine::clearTextureFloat(
    MTL::ComputeCommandEncoder* encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    const float clearValue[4]
)
{
    clearTexture(encoder, texture, subresourceRange, Type::Float, clearValue, sizeof(float[4]));
}

void ClearEngine::clearTexture(
    MTL::ComputeCommandEncoder* encoder,
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    Type type,
    const void* clearValue,
    size_t clearValueSize
)
{
    TextureType textureType = texture->m_desc.type;

    encoder->setComputePipelineState(m_clearPipelines[size_t(textureType)][size_t(type)].get());
    encoder->setTexture(texture->m_texture.get(), 0);
    encoder->setBytes(clearValue, clearValueSize, 1);

    MTL::Size threadGroupSize = m_threadGroupSizes[size_t(textureType)];

    for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; ++mipOffset)
    {
        for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; ++layerOffset)
        {
            uint32_t mip = subresourceRange.mip + mipOffset;
            uint32_t layer = subresourceRange.layer + layerOffset;
            Extent3D mipSize = calcMipSize(texture->m_desc.size, mip);
            Params params = {};
            params.width = mipSize.width;
            params.height = mipSize.height;
            params.depth = mipSize.depth;
            params.layer = layer;
            params.mip = mip;
            encoder->setBytes(&params, sizeof(params), 0);

            MTL::Size threadGroups = MTL::Size{
                (params.width + threadGroupSize.width - 1) / threadGroupSize.width,
                (params.height + threadGroupSize.height - 1) / threadGroupSize.height,
                (params.depth + threadGroupSize.depth - 1) / threadGroupSize.depth
            };
            encoder->dispatchThreadgroups(threadGroups, threadGroupSize);
        }
    }
}

} // namespace rhi::metal

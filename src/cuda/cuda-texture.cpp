#include "cuda-texture.h"
#include "cuda-device.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

#define FLAG_INT 0x1
#define FLAG_SRGB 0x2

struct FormatMapping
{
    Format format;
    CUarray_format arrayFormat;
    uint32_t elementSize;
    uint32_t channelCount;
    uint32_t flags;
};

inline const FormatMapping& getFormatMapping(Format format)
{
    static const FormatMapping mappings[] = {
        // clang-format off
        // format                   arrayFormat                     es  cc  flags
        { Format::Undefined,        CUarray_format(0),              0,  0,  0           },

        { Format::R8Uint,           CU_AD_FORMAT_UNSIGNED_INT8,     1,  1,  FLAG_INT    },
        { Format::R8Sint,           CU_AD_FORMAT_SIGNED_INT8,       1,  1,  FLAG_INT    },
        { Format::R8Unorm,          CU_AD_FORMAT_UNORM_INT8X1,      1,  1,  0           },
        { Format::R8Snorm,          CU_AD_FORMAT_SNORM_INT8X1,      1,  1,  0           },

        { Format::RG8Uint,          CU_AD_FORMAT_UNSIGNED_INT8,     2,  2,  FLAG_INT    },
        { Format::RG8Sint,          CU_AD_FORMAT_SIGNED_INT8,       2,  2,  FLAG_INT    },
        { Format::RG8Unorm,         CU_AD_FORMAT_UNORM_INT8X2,      2,  2,  0           },
        { Format::RG8Snorm,         CU_AD_FORMAT_SNORM_INT8X2,      2,  2,  0           },

        { Format::RGBA8Uint,        CU_AD_FORMAT_UNSIGNED_INT8,     4,  4,  FLAG_INT    },
        { Format::RGBA8Sint,        CU_AD_FORMAT_SIGNED_INT8,       4,  4,  FLAG_INT    },
        { Format::RGBA8Unorm,       CU_AD_FORMAT_UNORM_INT8X4,      4,  4,  0           },
        { Format::RGBA8UnormSrgb,   CU_AD_FORMAT_UNORM_INT8X4,      4,  4,  FLAG_SRGB   },
        { Format::RGBA8Snorm,       CU_AD_FORMAT_SNORM_INT8X4,      4,  4,  0           },
        { Format::BGRA8Unorm,       CUarray_format(0),              4,  4,  0           },
        { Format::BGRA8UnormSrgb,   CUarray_format(0),              4,  4,  0           },
        { Format::BGRX8Unorm,       CUarray_format(0),              4,  4,  0           },
        { Format::BGRX8UnormSrgb,   CUarray_format(0),              4,  4,  0           },

        { Format::R16Uint,          CU_AD_FORMAT_UNSIGNED_INT16,    2,  1,  FLAG_INT    },
        { Format::R16Sint,          CU_AD_FORMAT_SIGNED_INT16,      2,  1,  FLAG_INT    },
        { Format::R16Unorm,         CU_AD_FORMAT_UNORM_INT16X1,     2,  1,  0           },
        { Format::R16Snorm,         CU_AD_FORMAT_SNORM_INT16X1,     2,  1,  0           },
        { Format::R16Float,         CU_AD_FORMAT_HALF,              2,  1,  0           },

        { Format::RG16Uint,         CU_AD_FORMAT_UNSIGNED_INT16,    4,  2,  FLAG_INT    },
        { Format::RG16Sint,         CU_AD_FORMAT_SIGNED_INT16,      4,  2,  FLAG_INT    },
        { Format::RG16Unorm,        CU_AD_FORMAT_UNORM_INT16X2,     4,  2,  0           },
        { Format::RG16Snorm,        CU_AD_FORMAT_SNORM_INT16X2,     4,  2,  0           },
        { Format::RG16Float,        CU_AD_FORMAT_HALF,              4,  2,  0           },

        { Format::RGBA16Uint,       CU_AD_FORMAT_UNSIGNED_INT16,    8,  4,  FLAG_INT    },
        { Format::RGBA16Sint,       CU_AD_FORMAT_SIGNED_INT16,      8,  4,  FLAG_INT    },
        { Format::RGBA16Unorm,      CU_AD_FORMAT_UNORM_INT16X4,     8,  4,  0           },
        { Format::RGBA16Snorm,      CU_AD_FORMAT_SNORM_INT16X4,     8,  4,  0           },
        { Format::RGBA16Float,      CU_AD_FORMAT_HALF,              8,  4,  0           },

        { Format::R32Uint,          CU_AD_FORMAT_UNSIGNED_INT32,    4,  1,  FLAG_INT    },
        { Format::R32Sint,          CU_AD_FORMAT_SIGNED_INT32,      4,  1,  FLAG_INT    },
        { Format::R32Float,         CU_AD_FORMAT_FLOAT,             4,  1,  0           },

        { Format::RG32Uint,         CU_AD_FORMAT_UNSIGNED_INT32,    8,  2,  FLAG_INT    },
        { Format::RG32Sint,         CU_AD_FORMAT_SIGNED_INT32,      8,  2,  FLAG_INT    },
        { Format::RG32Float,        CU_AD_FORMAT_FLOAT,             8,  2,  0           },

        { Format::RGB32Uint,        CUarray_format(0),              0,  0,  0           },
        { Format::RGB32Sint,        CUarray_format(0),              0,  0,  0           },
        { Format::RGB32Float,       CUarray_format(0),              0,  0,  0           },

        { Format::RGBA32Uint,       CU_AD_FORMAT_UNSIGNED_INT32,    16, 4,  FLAG_INT    },
        { Format::RGBA32Sint,       CU_AD_FORMAT_SIGNED_INT32,      16, 4,  FLAG_INT    },
        { Format::RGBA32Float,      CU_AD_FORMAT_FLOAT,             16, 4,  0           },

        { Format::R64Uint,          CUarray_format(0),              0,  0,  0           },
        { Format::R64Sint,          CUarray_format(0),              0,  0,  0           },

        { Format::BGRA4Unorm,       CUarray_format(0),              0,  0,  0           },
        { Format::B5G6R5Unorm,      CUarray_format(0),              0,  0,  0           },
        { Format::BGR5A1Unorm,      CUarray_format(0),              0,  0,  0           },
        { Format::RGB9E5Ufloat,     CUarray_format(0),              0,  0,  0           },
        { Format::RGB10A2Uint,      CUarray_format(0),              0,  0,  0           },
        { Format::RGB10A2Unorm,     CUarray_format(0),              0,  0,  0           },
        { Format::R11G11B10Float,   CUarray_format(0),              0,  0,  0           },

        { Format::D32Float,         CU_AD_FORMAT_FLOAT,             4,  1,  0           },
        { Format::D16Unorm,         CUarray_format(0),              0,  0,  0           },
        { Format::D32FloatS8Uint,   CUarray_format(0),              0,  0,  0           },

        { Format::BC1Unorm,         CU_AD_FORMAT_BC1_UNORM,         8,  4,  0           },
        { Format::BC1UnormSrgb,     CU_AD_FORMAT_BC1_UNORM_SRGB,    8,  4,  FLAG_SRGB   },
        { Format::BC2Unorm,         CU_AD_FORMAT_BC2_UNORM,         16, 4,  0           },
        { Format::BC2UnormSrgb,     CU_AD_FORMAT_BC2_UNORM_SRGB,    16, 4,  FLAG_SRGB   },
        { Format::BC3Unorm,         CU_AD_FORMAT_BC3_UNORM,         16, 4,  0           },
        { Format::BC3UnormSrgb,     CU_AD_FORMAT_BC3_UNORM_SRGB,    16, 4,  FLAG_SRGB   },
        { Format::BC4Unorm,         CU_AD_FORMAT_BC4_UNORM,         8,  1,  0           },
        { Format::BC4Snorm,         CU_AD_FORMAT_BC4_SNORM,         8,  1,  0           },
        { Format::BC5Unorm,         CU_AD_FORMAT_BC5_UNORM,         16, 2,  0           },
        { Format::BC5Snorm,         CU_AD_FORMAT_BC5_SNORM,         16, 2,  0           },
        { Format::BC6HUfloat,       CU_AD_FORMAT_BC6H_UF16,         16, 3,  0           },
        { Format::BC6HSfloat,       CU_AD_FORMAT_BC6H_SF16,         16, 3,  0           },
        { Format::BC7Unorm,         CU_AD_FORMAT_BC7_UNORM,         16, 4,  0           },
        { Format::BC7UnormSrgb,     CU_AD_FORMAT_BC7_UNORM_SRGB,    16, 4,  FLAG_SRGB   },
        // clang-format on
    };

    static_assert(SLANG_COUNT_OF(mappings) == size_t(Format::_Count), "Missing format mapping");
    SLANG_RHI_ASSERT(uint32_t(format) < uint32_t(Format::_Count));
    return mappings[int(format)];
}

bool isFormatSupported(Format format)
{
    return getFormatMapping(format).arrayFormat != CUarray_format(0);
}

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    for (auto& pair : m_texObjects)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuTexObjectDestroy(pair.second));
    }
    for (auto& pair : m_surfObjects)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuSurfObjectDestroy(pair.second));
    }
    if (m_cudaArray)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuArrayDestroy(m_cudaArray));
    }
    if (m_cudaMipMappedArray)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayDestroy(m_cudaMipMappedArray));
    }
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    if (m_cudaArray)
    {
        outHandle->type = NativeHandleType::CUarray;
        outHandle->value = (uint64_t)m_cudaArray;
        return SLANG_OK;
    }
    else if (m_cudaMipMappedArray)
    {
        outHandle->type = NativeHandleType::CUmipmappedArray;
        outHandle->value = (uint64_t)m_cudaMipMappedArray;
        return SLANG_OK;
    }
    return SLANG_FAIL;
}

CUtexObject TextureImpl::getTexObject(Format format, const SubresourceRange& range)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ViewKey key = {format, range};
    CUtexObject& texObject = m_texObjects[key];
    if (texObject)
        return texObject;

    SLANG_RHI_ASSERT(m_cudaArray || m_cudaMipMappedArray);
    CUDA_RESOURCE_DESC resDesc = {};
    if (m_cudaArray)
    {
        resDesc.resType = CU_RESOURCE_TYPE_ARRAY;
        resDesc.res.array.hArray = m_cudaArray;
    }
    else
    {
        resDesc.resType = CU_RESOURCE_TYPE_MIPMAPPED_ARRAY;
        resDesc.res.mipmap.hMipmappedArray = m_cudaMipMappedArray;
    }

    // TODO provide a way to set these parameters
    CUDA_TEXTURE_DESC texDesc = {};
    texDesc.addressMode[0] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.addressMode[1] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.addressMode[2] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texDesc.flags = CU_TRSF_NORMALIZED_COORDINATES;
    const FormatMapping& mapping = getFormatMapping(format);
    if (mapping.flags & FLAG_INT)
        texDesc.flags |= CU_TRSF_READ_AS_INTEGER;
    if (mapping.flags & FLAG_SRGB)
        texDesc.flags |= CU_TRSF_SRGB;

    CUDA_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.format = CU_RES_VIEW_FORMAT_NONE; // Use underlaying format
    viewDesc.width = m_desc.size.width;
    viewDesc.height = m_desc.size.height;
    viewDesc.depth = m_desc.size.depth;
    viewDesc.firstMipmapLevel = range.mip;
    viewDesc.lastMipmapLevel = range.mip + range.mipCount - 1;
    viewDesc.firstLayer = range.layer;
    viewDesc.lastLayer = range.layer + range.layerCount - 1;

    SLANG_CUDA_ASSERT_ON_FAIL(
        cuTexObjectCreate(&texObject, &resDesc, &texDesc, isEntireTexture(range) ? nullptr : &viewDesc)
    );
    return texObject;
}

CUsurfObject TextureImpl::getSurfObject(const SubresourceRange& range)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CUsurfObject& surfObject = m_surfObjects[range];
    if (surfObject)
        return surfObject;

    CUarray array = m_cudaArray;
    if (!array)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayGetLevel(&array, m_cudaMipMappedArray, range.mip));
    }

    CUDA_RESOURCE_DESC resDesc = {};
    resDesc.resType = CU_RESOURCE_TYPE_ARRAY;
    resDesc.res.array.hArray = array;

    SLANG_CUDA_ASSERT_ON_FAIL(cuSurfObjectCreate(&surfObject, &resDesc));
    return surfObject;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    SLANG_CUDA_CTX_SCOPE(this);

    TextureDesc desc = fixupTextureDesc(desc_);

    RefPtr<TextureImpl> tex = new TextureImpl(this, desc);

    // The size of the element/texel in bytes
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    const FormatMapping& mapping = getFormatMapping(desc.format);
    if (mapping.arrayFormat == 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    switch (desc.type)
    {
    case TextureType::Texture1D:
        if (desc.mipCount == 1)
        {
            CUDA_ARRAY_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Format = mapping.arrayFormat;
            arrayDesc.NumChannels = mapping.channelCount;
            SLANG_CUDA_RETURN_ON_FAIL(cuArrayCreate(&tex->m_cudaArray, &arrayDesc));
        }
        else
        {
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Format = mapping.arrayFormat;
            arrayDesc.NumChannels = mapping.channelCount;
            SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount));
        }
        break;
    case TextureType::Texture1DArray:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Depth = desc.arrayLength;
        arrayDesc.Format = mapping.arrayFormat;
        arrayDesc.NumChannels = mapping.channelCount;
        arrayDesc.Flags = CUDA_ARRAY3D_LAYERED;
        SLANG_CUDA_RETURN_ON_FAIL(
            desc.mipCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                               : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount)
        );
        break;
    }
    case TextureType::Texture2D:
    {
        if (desc.mipCount == 1)
        {
            CUDA_ARRAY_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Height = desc.size.height;
            arrayDesc.Format = mapping.arrayFormat;
            arrayDesc.NumChannels = mapping.channelCount;
            SLANG_CUDA_RETURN_ON_FAIL(cuArrayCreate(&tex->m_cudaArray, &arrayDesc));
        }
        else
        {
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Height = desc.size.height;
            arrayDesc.Format = mapping.arrayFormat;
            arrayDesc.NumChannels = mapping.channelCount;
            SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount));
        }
        break;
    }
    case TextureType::Texture2DArray:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Height = desc.size.height;
        arrayDesc.Depth = desc.arrayLength;
        arrayDesc.Format = mapping.arrayFormat;
        arrayDesc.NumChannels = mapping.channelCount;
        arrayDesc.Flags = CUDA_ARRAY3D_LAYERED;
        SLANG_CUDA_RETURN_ON_FAIL(
            desc.mipCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                               : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount)
        );
        break;
    }
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        return SLANG_E_NOT_AVAILABLE;
    case TextureType::Texture3D:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Height = desc.size.height;
        arrayDesc.Depth = desc.size.depth;
        arrayDesc.Format = mapping.arrayFormat;
        arrayDesc.NumChannels = mapping.channelCount;
        SLANG_CUDA_RETURN_ON_FAIL(
            desc.mipCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                               : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount)
        );
        break;
    }
    case TextureType::TextureCube:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Height = desc.size.height;
        arrayDesc.Depth = 6;
        arrayDesc.Format = mapping.arrayFormat;
        arrayDesc.NumChannels = mapping.channelCount;
        arrayDesc.Flags = CUDA_ARRAY3D_CUBEMAP;
        SLANG_CUDA_RETURN_ON_FAIL(
            desc.mipCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                               : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount)
        );
        break;
    }
    case TextureType::TextureCubeArray:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Height = desc.size.height;
        arrayDesc.Depth = desc.arrayLength * 6;
        arrayDesc.Format = mapping.arrayFormat;
        arrayDesc.NumChannels = mapping.channelCount;
        arrayDesc.Flags = CUDA_ARRAY3D_CUBEMAP | CUDA_ARRAY3D_LAYERED;
        SLANG_CUDA_RETURN_ON_FAIL(
            desc.mipCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                               : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount)
        );
        break;
    }
    }

    if (initData)
    {
        uint32_t mipCount = desc.mipCount;
        uint32_t layerCount = desc.getLayerCount();
        uint32_t subresourceIndex = 0;

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mip = 0; mip < mipCount; ++mip)
            {
                const SubresourceData& subresourceData = initData[subresourceIndex++];

                Extent3D mipSize = calcMipSize(desc.size, mip);

                CUarray dstArray = tex->m_cudaArray;
                if (tex->m_cudaMipMappedArray)
                {
                    SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayGetLevel(&dstArray, tex->m_cudaMipMappedArray, mip));
                }

                CUDA_MEMCPY3D copyParam = {};
                copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
                copyParam.dstArray = dstArray;
                copyParam.dstZ = layer;
                copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
                copyParam.srcHost = subresourceData.data;
                copyParam.srcPitch = subresourceData.rowPitch;
                copyParam.srcHeight = heightInBlocks(formatInfo, mipSize.height);
                copyParam.WidthInBytes = widthInBlocks(formatInfo, mipSize.width) * mapping.elementSize;
                copyParam.Height = heightInBlocks(formatInfo, mipSize.height);
                copyParam.Depth = mipSize.depth;
                SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy3D(&copyParam));
            }
        }
    }

    returnComPtr(outTexture, tex);
    return SLANG_OK;
}

Result DeviceImpl::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& desc,
    const size_t size,
    ITexture** outTexture
)
{
    if (!handle)
    {
        *outTexture = nullptr;
        return SLANG_OK;
    }

    RefPtr<TextureImpl> texture = new TextureImpl(this, desc);

    // CUDA manages sharing of buffers through the idea of an
    // "external memory" object, which represents the relationship
    // with another API's objects. In order to create this external
    // memory association, we first need to fill in a descriptor struct.
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC externalMemoryHandleDesc;
    memset(&externalMemoryHandleDesc, 0, sizeof(externalMemoryHandleDesc));
    switch (handle.type)
    {
    case NativeHandleType::D3D12Resource:
        externalMemoryHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
        break;
    case NativeHandleType::Win32:
        externalMemoryHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
        break;
    default:
        return SLANG_FAIL;
    }
    externalMemoryHandleDesc.handle.win32.handle = (void*)handle.value;
    externalMemoryHandleDesc.size = size;
    externalMemoryHandleDesc.flags = 0; // CUDA_EXTERNAL_MEMORY_DEDICATED;

    CUexternalMemory externalMemory;
    SLANG_CUDA_RETURN_ON_FAIL(cuImportExternalMemory(&externalMemory, &externalMemoryHandleDesc));
    texture->m_cudaExternalMemory = externalMemory;

    const FormatMapping& mapping = getFormatMapping(desc.format);
    if (mapping.arrayFormat == 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
    arrayDesc.Depth = desc.size.depth;
    arrayDesc.Height = desc.size.height;
    arrayDesc.Width = desc.size.width;
    arrayDesc.Format = mapping.arrayFormat;
    arrayDesc.NumChannels = mapping.channelCount;
    arrayDesc.Flags = 0; // TODO: Flags? CUDA_ARRAY_LAYERED/SURFACE_LDST/CUBEMAP/TEXTURE_GATHER

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC externalMemoryMipDesc;
    memset(&externalMemoryMipDesc, 0, sizeof(externalMemoryMipDesc));
    externalMemoryMipDesc.offset = 0;
    externalMemoryMipDesc.arrayDesc = arrayDesc;
    externalMemoryMipDesc.numLevels = desc.mipCount;

    CUmipmappedArray mipArray;
    SLANG_CUDA_RETURN_ON_FAIL(cuExternalMemoryGetMappedMipmappedArray(&mipArray, externalMemory, &externalMemoryMipDesc)
    );
    texture->m_cudaMipMappedArray = mipArray;

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::cuda

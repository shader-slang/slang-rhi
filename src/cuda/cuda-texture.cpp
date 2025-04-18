#include "cuda-texture.h"
#include "cuda-device.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

struct FormatMapping
{
    Format format;
    CUarray_format arrayFormat;
    uint32_t channelCount;
    CUresourceViewFormat viewFormat;
};

const FormatMapping& getFormatMapping(Format format)
{
    static const FormatMapping mappings[] = {
        // clang-format off
        // format                   arrayFormat                     cc  viewFormat
        { Format::Undefined,        CUarray_format(0),              0,  CUresourceViewFormat(0)         },

        { Format::R8Uint,           CU_AD_FORMAT_UNSIGNED_INT8,     1,  CU_RES_VIEW_FORMAT_UINT_1X8     },
        { Format::R8Sint,           CU_AD_FORMAT_SIGNED_INT8,       1,  CU_RES_VIEW_FORMAT_SINT_1X8     },
        { Format::R8Unorm,          CU_AD_FORMAT_UNORM_INT8X1,      1,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::R8Snorm,          CU_AD_FORMAT_SNORM_INT8X1,      1,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::RG8Uint,          CU_AD_FORMAT_UNSIGNED_INT8,     2,  CU_RES_VIEW_FORMAT_UINT_2X8     },
        { Format::RG8Sint,          CU_AD_FORMAT_SIGNED_INT8,       2,  CU_RES_VIEW_FORMAT_SINT_2X8     },
        { Format::RG8Unorm,         CU_AD_FORMAT_UNORM_INT8X2,      2,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RG8Snorm,         CU_AD_FORMAT_SNORM_INT8X2,      2,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::RGBA8Uint,        CU_AD_FORMAT_UNSIGNED_INT8,     4,  CU_RES_VIEW_FORMAT_UINT_4X8     },
        { Format::RGBA8Sint,        CU_AD_FORMAT_SIGNED_INT8,       4,  CU_RES_VIEW_FORMAT_SINT_4X8     },
        { Format::RGBA8Unorm,       CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGBA8UnormSrgb,   CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGBA8Snorm,       CU_AD_FORMAT_SNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::BGRA8Unorm,       CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::BGRA8UnormSrgb,   CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::BGRX8Unorm,       CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::BGRX8UnormSrgb,   CU_AD_FORMAT_UNORM_INT8X4,      4,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::R16Uint,          CU_AD_FORMAT_UNSIGNED_INT16,    1,  CU_RES_VIEW_FORMAT_UINT_1X16    },
        { Format::R16Sint,          CU_AD_FORMAT_SIGNED_INT16,      1,  CU_RES_VIEW_FORMAT_SINT_1X16    },
        { Format::R16Unorm,         CU_AD_FORMAT_UNORM_INT16X1,     1,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::R16Snorm,         CU_AD_FORMAT_SNORM_INT16X1,     1,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::R16Float,         CU_AD_FORMAT_HALF,              1,  CU_RES_VIEW_FORMAT_FLOAT_1X16   },

        { Format::RG16Uint,         CU_AD_FORMAT_UNSIGNED_INT16,    2,  CU_RES_VIEW_FORMAT_UINT_2X16    },
        { Format::RG16Sint,         CU_AD_FORMAT_SIGNED_INT16,      2,  CU_RES_VIEW_FORMAT_SINT_2X16    },
        { Format::RG16Unorm,        CU_AD_FORMAT_UNORM_INT16X2,     2,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RG16Snorm,        CU_AD_FORMAT_SNORM_INT16X2,     2,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RG16Float,        CU_AD_FORMAT_HALF,              2,  CU_RES_VIEW_FORMAT_FLOAT_2X16   },

        { Format::RGBA16Uint,       CU_AD_FORMAT_UNSIGNED_INT16,    4,  CU_RES_VIEW_FORMAT_UINT_4X16    },
        { Format::RGBA16Sint,       CU_AD_FORMAT_SIGNED_INT16,      4,  CU_RES_VIEW_FORMAT_SINT_4X16    },
        { Format::RGBA16Unorm,      CU_AD_FORMAT_UNORM_INT16X4,     4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGBA16Snorm,      CU_AD_FORMAT_SNORM_INT16X4,     4,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGBA16Float,      CU_AD_FORMAT_HALF,              4,  CU_RES_VIEW_FORMAT_FLOAT_4X16   },

        { Format::R32Uint,          CU_AD_FORMAT_UNSIGNED_INT32,    1,  CU_RES_VIEW_FORMAT_UINT_1X32    },
        { Format::R32Sint,          CU_AD_FORMAT_SIGNED_INT32,      1,  CU_RES_VIEW_FORMAT_SINT_1X32    },
        { Format::R32Float,         CU_AD_FORMAT_FLOAT,             1,  CU_RES_VIEW_FORMAT_FLOAT_1X32   },

        { Format::RG32Uint,         CU_AD_FORMAT_UNSIGNED_INT32,    2,  CU_RES_VIEW_FORMAT_UINT_2X32    },
        { Format::RG32Sint,         CU_AD_FORMAT_SIGNED_INT32,      2,  CU_RES_VIEW_FORMAT_SINT_2X32    },
        { Format::RG32Float,        CU_AD_FORMAT_FLOAT,             2,  CU_RES_VIEW_FORMAT_FLOAT_2X32   },

        { Format::RGB32Uint,        CU_AD_FORMAT_UNSIGNED_INT32,    3,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGB32Sint,        CU_AD_FORMAT_SIGNED_INT32,      3,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGB32Float,       CU_AD_FORMAT_FLOAT,             3,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::RGBA32Uint,       CU_AD_FORMAT_UNSIGNED_INT32,    4,  CU_RES_VIEW_FORMAT_UINT_4X32    },
        { Format::RGBA32Sint,       CU_AD_FORMAT_SIGNED_INT32,      4,  CU_RES_VIEW_FORMAT_SINT_4X32    },
        { Format::RGBA32Float,      CU_AD_FORMAT_FLOAT,             4,  CU_RES_VIEW_FORMAT_FLOAT_4X32   },

        { Format::R64Uint,          CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::R64Sint,          CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::BGRA4Unorm,       CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::B5G6R5Unorm,      CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::BGR5A1Unorm,      CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGB9E5Ufloat,     CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGB10A2Uint,      CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::RGB10A2Unorm,     CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::R11G11B10Float,   CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::D32Float,         CU_AD_FORMAT_FLOAT,             1,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::D16Unorm,         CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },
        { Format::D32FloatS8Uint,   CUarray_format(0),              0,  CU_RES_VIEW_FORMAT_NONE         },

        { Format::BC1Unorm,         CU_AD_FORMAT_BC1_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC1 },
        { Format::BC1UnormSrgb,     CU_AD_FORMAT_BC1_UNORM_SRGB,    0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC1 },
        { Format::BC2Unorm,         CU_AD_FORMAT_BC2_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC2 },
        { Format::BC2UnormSrgb,     CU_AD_FORMAT_BC2_UNORM_SRGB,    0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC2 },
        { Format::BC3Unorm,         CU_AD_FORMAT_BC3_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC3 },
        { Format::BC3UnormSrgb,     CU_AD_FORMAT_BC3_UNORM_SRGB,    0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC3 },
        { Format::BC4Unorm,         CU_AD_FORMAT_BC4_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC4 },
        { Format::BC4Snorm,         CU_AD_FORMAT_BC4_SNORM,         0,  CU_RES_VIEW_FORMAT_SIGNED_BC4   },
        { Format::BC5Unorm,         CU_AD_FORMAT_BC5_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC5 },
        { Format::BC5Snorm,         CU_AD_FORMAT_BC5_SNORM,         0,  CU_RES_VIEW_FORMAT_SIGNED_BC5   },
        { Format::BC6HUfloat,       CU_AD_FORMAT_BC6H_UF16,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC6H},
        { Format::BC6HSfloat,       CU_AD_FORMAT_BC6H_SF16,         0,  CU_RES_VIEW_FORMAT_SIGNED_BC6H  },
        { Format::BC7Unorm,         CU_AD_FORMAT_BC7_UNORM,         0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC7 },
        { Format::BC7UnormSrgb,     CU_AD_FORMAT_BC7_UNORM_SRGB,    0,  CU_RES_VIEW_FORMAT_UNSIGNED_BC7 },
        // clang-format on
    };

    static_assert(SLANG_COUNT_OF(mappings) == size_t(Format::_Count), "Missing format mapping");
    SLANG_RHI_ASSERT(uint32_t(format) < uint32_t(Format::_Count));
    return mappings[int(format)];
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
    size_t elementSize = 0;
    CUarray_format format = CU_AD_FORMAT_FLOAT;
    int numChannels = 0;

    SLANG_RETURN_ON_FAIL(getCUDAFormat(desc.format, &format));
    const FormatInfo& info = getFormatInfo(desc.format);
    numChannels = info.channelCount;

    switch (format)
    {
    case CU_AD_FORMAT_UNSIGNED_INT8:
    case CU_AD_FORMAT_SIGNED_INT8:
        elementSize = 1 * numChannels;
        break;
    case CU_AD_FORMAT_UNSIGNED_INT16:
    case CU_AD_FORMAT_SIGNED_INT16:
        elementSize = 2 * numChannels;
        break;
    case CU_AD_FORMAT_UNSIGNED_INT32:
    case CU_AD_FORMAT_SIGNED_INT32:
        elementSize = 4 * numChannels;
        break;
    case CU_AD_FORMAT_HALF:
        elementSize = 2 * numChannels;
        break;
    case CU_AD_FORMAT_FLOAT:
        elementSize = 4 * numChannels;
        break;
    default:
    {
        SLANG_RHI_ASSERT_FAILURE("Unsupported format");
        return SLANG_FAIL;
    }
    }

    switch (desc.type)
    {
    case TextureType::Texture1D:
        if (desc.mipCount == 1)
        {
            CUDA_ARRAY_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Format = format;
            arrayDesc.NumChannels = numChannels;
            SLANG_CUDA_RETURN_ON_FAIL(cuArrayCreate(&tex->m_cudaArray, &arrayDesc));
        }
        else
        {
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Format = format;
            arrayDesc.NumChannels = numChannels;
            SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipCount));
        }
        break;
    case TextureType::Texture1DArray:
    {
        CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = desc.size.width;
        arrayDesc.Depth = desc.arrayLength;
        arrayDesc.Format = format;
        arrayDesc.NumChannels = numChannels;
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
            arrayDesc.Format = format;
            arrayDesc.NumChannels = numChannels;
            SLANG_CUDA_RETURN_ON_FAIL(cuArrayCreate(&tex->m_cudaArray, &arrayDesc));
        }
        else
        {
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = desc.size.width;
            arrayDesc.Height = desc.size.height;
            arrayDesc.Format = format;
            arrayDesc.NumChannels = numChannels;
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
        arrayDesc.Format = format;
        arrayDesc.NumChannels = numChannels;
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
        arrayDesc.Format = format;
        arrayDesc.NumChannels = numChannels;
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
        arrayDesc.Format = format;
        arrayDesc.NumChannels = numChannels;
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
        arrayDesc.Format = format;
        arrayDesc.NumChannels = numChannels;
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
                copyParam.WidthInBytes = mipSize.width * elementSize;
                copyParam.Height = mipSize.height;
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

    const FormatInfo formatInfo = getFormatInfo(desc.format);
    CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
    arrayDesc.Depth = desc.size.depth;
    arrayDesc.Height = desc.size.height;
    arrayDesc.Width = desc.size.width;
    arrayDesc.NumChannels = formatInfo.channelCount;
    getCUDAFormat(desc.format, &arrayDesc.Format);
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

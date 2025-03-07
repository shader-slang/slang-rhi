#include "cuda-texture.h"
#include "cuda-device.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    if (m_cudaSurfObj)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuSurfObjectDestroy(m_cudaSurfObj));
    }
    if (m_cudaTexObj)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuTexObjectDestroy(m_cudaTexObj));
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

Result DeviceImpl::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc srcDesc = fixupTextureDesc(desc);

    RefPtr<TextureImpl> tex = new TextureImpl(this, srcDesc);

    // CUresourcetype resourceType = CU_RESOURCE_TYPE_ARRAY;

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
        if (desc.mipLevelCount == 1)
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
            SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
            );
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
            desc.mipLevelCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                                    : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
        );
        break;
    }
    case TextureType::Texture2D:
    {
        if (desc.mipLevelCount == 1)
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
            SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
            );
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
            desc.mipLevelCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                                    : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
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
            desc.mipLevelCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                                    : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
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
            desc.mipLevelCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                                    : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
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
            desc.mipLevelCount == 1 ? cuArray3DCreate(&tex->m_cudaArray, &arrayDesc)
                                    : cuMipmappedArrayCreate(&tex->m_cudaMipMappedArray, &arrayDesc, desc.mipLevelCount)
        );
        break;
    }
    }

    // Work space for holding data for uploading if it needs to be rearranged
    if (initData)
    {
        std::vector<uint8_t> workspace;
        for (int mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            int mipWidth = desc.size.width >> mipLevel;
            int mipHeight = desc.size.height >> mipLevel;
            int mipDepth = desc.size.depth >> mipLevel;

            mipWidth = (mipWidth == 0) ? 1 : mipWidth;
            mipHeight = (mipHeight == 0) ? 1 : mipHeight;
            mipDepth = (mipDepth == 0) ? 1 : mipDepth;

            // If it's a cubemap then the depth is always 6
            if (desc.type == TextureType::TextureCube)
            {
                mipDepth = 6;
            }

            auto dstArray = tex->m_cudaArray;
            if (tex->m_cudaMipMappedArray)
            {
                // Get the array for the mip level
                SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayGetLevel(&dstArray, tex->m_cudaMipMappedArray, mipLevel));
            }
            SLANG_RHI_ASSERT(dstArray);

            // Check using the desc to see if it's plausible
            {
                CUDA_ARRAY_DESCRIPTOR arrayDesc;
                SLANG_CUDA_RETURN_ON_FAIL(cuArrayGetDescriptor(&arrayDesc, dstArray));

                SLANG_RHI_ASSERT(mipWidth == arrayDesc.Width);
                SLANG_RHI_ASSERT(mipHeight == arrayDesc.Height || (mipHeight == 1 && arrayDesc.Height == 0));
            }

            const void* srcDataPtr = nullptr;

            if (desc.arrayLength > 1)
            {
                SLANG_RHI_ASSERT(
                    desc.type == TextureType::Texture1D || desc.type == TextureType::Texture2D ||
                    desc.type == TextureType::TextureCube
                );

                // TODO(JS): Here I assume that arrays are just held contiguously within a
                // 'face' This seems reasonable and works with the Copy3D.
                const size_t faceSizeInBytes = elementSize * mipWidth * mipHeight;

                uint32_t faceCount = desc.arrayLength;
                if (desc.type == TextureType::TextureCube)
                {
                    faceCount *= 6;
                }

                const size_t mipSizeInBytes = faceSizeInBytes * faceCount;
                workspace.resize(mipSizeInBytes);

                // We need to add the face data from each mip
                // We iterate over face count so we copy all of the cubemap faces
                for (uint32_t j = 0; j < faceCount; j++)
                {
                    const auto srcData = initData[mipLevel + j * desc.mipLevelCount].data;
                    // Copy over to the workspace to make contiguous
                    ::memcpy(workspace.data() + faceSizeInBytes * j, srcData, faceSizeInBytes);
                }

                srcDataPtr = workspace.data();
            }
            else
            {
                if (desc.type == TextureType::TextureCube)
                {
                    size_t faceSizeInBytes = elementSize * mipWidth * mipHeight;

                    workspace.resize(faceSizeInBytes * 6);
                    // Copy the data over to make contiguous
                    for (uint32_t j = 0; j < 6; j++)
                    {
                        const auto srcData = initData[mipLevel + j * desc.mipLevelCount].data;
                        ::memcpy(workspace.data() + faceSizeInBytes * j, srcData, faceSizeInBytes);
                    }
                    srcDataPtr = workspace.data();
                }
                else
                {
                    const auto srcData = initData[mipLevel].data;
                    srcDataPtr = srcData;
                }
            }

            if (desc.arrayLength > 1)
            {
                SLANG_RHI_ASSERT(
                    desc.type == TextureType::Texture1D || desc.type == TextureType::Texture2D ||
                    desc.type == TextureType::TextureCube
                );

                CUDA_MEMCPY3D copyParam = {};
                copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
                copyParam.dstArray = dstArray;
                copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
                copyParam.srcHost = srcDataPtr;
                copyParam.srcPitch = mipWidth * elementSize;
                copyParam.WidthInBytes = copyParam.srcPitch;
                copyParam.Height = mipHeight;
                // Set the depth to the array length
                copyParam.Depth = desc.arrayLength;

                if (desc.type == TextureType::TextureCube)
                {
                    copyParam.Depth *= 6;
                }

                SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy3D(&copyParam));
            }
            else
            {
                switch (desc.type)
                {
                case TextureType::Texture1D:
                case TextureType::Texture2D:
                {
                    CUDA_MEMCPY2D copyParam = {};
                    copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
                    copyParam.dstArray = dstArray;
                    copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
                    copyParam.srcHost = srcDataPtr;
                    copyParam.srcPitch = mipWidth * elementSize;
                    copyParam.WidthInBytes = copyParam.srcPitch;
                    copyParam.Height = mipHeight;
                    SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy2D(&copyParam));
                    break;
                }
                case TextureType::Texture3D:
                case TextureType::TextureCube:
                {
                    CUDA_MEMCPY3D copyParam = {};
                    copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
                    copyParam.dstArray = dstArray;
                    copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
                    copyParam.srcHost = srcDataPtr;
                    copyParam.srcPitch = mipWidth * elementSize;
                    copyParam.WidthInBytes = copyParam.srcPitch;
                    copyParam.Height = mipHeight;
                    copyParam.Depth = mipDepth;

                    SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy3D(&copyParam));
                    break;
                }

                default:
                {
                    SLANG_RHI_ASSERT_FAILURE("Not implemented");
                    break;
                }
                }
            }
        }
    }
    // Set up texture sampling parameters, and create final texture obj

    {
        CUDA_RESOURCE_DESC resDesc = {};

        if (tex->m_cudaArray)
        {
            resDesc.resType = CU_RESOURCE_TYPE_ARRAY;
            resDesc.res.array.hArray = tex->m_cudaArray;
        }
        if (tex->m_cudaMipMappedArray)
        {
            resDesc.resType = CU_RESOURCE_TYPE_MIPMAPPED_ARRAY;
            resDesc.res.mipmap.hMipmappedArray = tex->m_cudaMipMappedArray;
        }

        // If the texture might be used as a UAV, then we need to allocate
        // a CUDA "surface" for it.
        //
        // Note: We cannot do this unconditionally, because it will fail
        // on surfaces that are not usable as UAVs (e.g., those with
        // mipmaps).
        //
        // TODO: We should really only be allocating the array at the
        // time we create a resource, and then allocate the surface or
        // texture objects as part of view creation.
        //
        if (is_set(desc.usage, TextureUsage::UnorderedAccess))
        {
            // On CUDA surfaces only support a single MIP map
            SLANG_RHI_ASSERT(desc.mipLevelCount == 1);

            SLANG_CUDA_RETURN_ON_FAIL(cuSurfObjectCreate(&tex->m_cudaSurfObj, &resDesc));
        }

        // Create handle for sampling.
        CUDA_TEXTURE_DESC texDesc;
        memset(&texDesc, 0, sizeof(CUDA_TEXTURE_DESC));
        texDesc.addressMode[0] = CU_TR_ADDRESS_MODE_WRAP;
        texDesc.addressMode[1] = CU_TR_ADDRESS_MODE_WRAP;
        texDesc.addressMode[2] = CU_TR_ADDRESS_MODE_WRAP;
        texDesc.filterMode = CU_TR_FILTER_MODE_LINEAR;
        texDesc.flags = CU_TRSF_NORMALIZED_COORDINATES;

        SLANG_CUDA_RETURN_ON_FAIL(cuTexObjectCreate(&tex->m_cudaTexObj, &resDesc, &texDesc, nullptr));
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
    externalMemoryMipDesc.numLevels = desc.mipLevelCount;

    CUmipmappedArray mipArray;
    SLANG_CUDA_RETURN_ON_FAIL(cuExternalMemoryGetMappedMipmappedArray(&mipArray, externalMemory, &externalMemoryMipDesc)
    );
    texture->m_cudaMipMappedArray = mipArray;

    CUarray cuArray;
    SLANG_CUDA_RETURN_ON_FAIL(cuMipmappedArrayGetLevel(&cuArray, mipArray, 0));
    texture->m_cudaArray = cuArray;

    CUDA_RESOURCE_DESC surfDesc;
    memset(&surfDesc, 0, sizeof(surfDesc));
    surfDesc.resType = CU_RESOURCE_TYPE_ARRAY;
    surfDesc.res.array.hArray = cuArray;

    CUsurfObject surface;
    SLANG_CUDA_RETURN_ON_FAIL(cuSurfObjectCreate(&surface, &surfDesc));
    texture->m_cudaSurfObj = surface;

    // Create handle for sampling.
    CUDA_TEXTURE_DESC texDesc;
    memset(&texDesc, 0, sizeof(CUDA_TEXTURE_DESC));
    texDesc.addressMode[0] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.addressMode[1] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.addressMode[2] = CU_TR_ADDRESS_MODE_WRAP;
    texDesc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texDesc.flags = CU_TRSF_NORMALIZED_COORDINATES;

    SLANG_CUDA_RETURN_ON_FAIL(cuTexObjectCreate(&texture->m_cudaTexObj, &surfDesc, &texDesc, nullptr));

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::cuda

#include "cuda-texture.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

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

uint64_t TextureImpl::getBindlessHandle()
{
    return (uint64_t)m_cudaTexObj;
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUtexObject;
    outHandle->value = getBindlessHandle();
    return SLANG_OK;
}

} // namespace rhi::cuda

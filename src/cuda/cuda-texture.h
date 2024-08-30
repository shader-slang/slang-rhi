#pragma once

#include "cuda-base.h"
#include "cuda-context.h"

namespace rhi::cuda {

class TextureImpl : public Texture
{
public:
    TextureImpl(const Texture::Desc& desc)
        : Texture(desc)
    {
    }
    ~TextureImpl();

    uint64_t getBindlessHandle();

    // The texObject is for reading 'texture' like things. This is an opaque type, that's backed by
    // a long long
    CUtexObject m_cudaTexObj = CUtexObject();

    // The surfObj is for reading/writing 'texture like' things, but not for sampling.
    CUsurfObject m_cudaSurfObj = CUsurfObject();

    CUarray m_cudaArray = CUarray();
    CUmipmappedArray m_cudaMipMappedArray = CUmipmappedArray();

    void* m_cudaExternalMemory = nullptr;

    RefPtr<CUDAContext> m_cudaContext;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;
};

} // namespace rhi::cuda

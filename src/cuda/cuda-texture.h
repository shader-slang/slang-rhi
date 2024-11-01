#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class TextureImpl : public Texture
{
public:
    TextureImpl(const TextureDesc& desc)
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

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    RefPtr<TextureImpl> m_texture;
};

} // namespace rhi::cuda

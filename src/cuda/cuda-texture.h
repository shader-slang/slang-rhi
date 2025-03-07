#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // The texObject is for reading 'texture' like things. This is an opaque type, that's backed by
    // a long long
    CUtexObject m_cudaTexObj = 0;

    // The surfObj is for reading/writing 'texture like' things, but not for sampling.
    CUsurfObject m_cudaSurfObj = 0;

    // Texture is either stored in cuda array or mip mapped array.
    CUarray m_cudaArray = 0;
    CUmipmappedArray m_cudaMipMappedArray = 0;

    void* m_cudaExternalMemory = nullptr;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    RefPtr<TextureImpl> m_texture;
};

} // namespace rhi::cuda

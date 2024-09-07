#pragma once

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture.h"

namespace rhi::cuda {

class ResourceViewImpl : public ResourceViewBase
{
public:
    RefPtr<BufferImpl> buffer;
    RefPtr<TextureImpl> texture;
    void* proxyBuffer = nullptr;
};

} // namespace rhi::cuda

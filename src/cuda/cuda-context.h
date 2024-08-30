#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class CUDAContext : public RefObject
{
public:
    CUcontext m_context = nullptr;
    ~CUDAContext() { cuCtxDestroy(m_context); }
};

} // namespace rhi::cuda

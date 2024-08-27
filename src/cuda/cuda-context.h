// cuda-context.h
#pragma once
#include "cuda-base.h"

namespace rhi
{
using namespace Slang;

namespace cuda
{

class CUDAContext : public RefObject
{
public:
    CUcontext m_context = nullptr;
    ~CUDAContext() { cuCtxDestroy(m_context); }
};

} // namespace cuda
} // namespace rhi

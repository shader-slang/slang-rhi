#pragma once

#include "cuda-base.h"
#include "core/blob.h"

namespace rhi::cuda {

// Backend artifact with the original PTX retained for rejected-cache recovery.
// Only the opted-in compute path constructs this type.
class CubinArtifact : public BlobBase
{
public:
    ComPtr<ISlangBlob> ptx;
    ComPtr<ISlangBlob> cubin;
    ComPtr<ISlangBlob> cacheKey;
    bool cacheHit = false;

    SLANG_NO_THROW const void* SLANG_MCALL getBufferPointer() override { return cubin->getBufferPointer(); }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return cubin->getBufferSize(); }
};

class CubinCompiler
{
public:
    static bool isRequested();
    Result initialize(DeviceImpl* device);
    Result convert(ShaderProgram* program, CompiledEntryPoint& entryPoint);
    Result rebuild(ISlangBlob* ptx, ISlangBlob* cacheKey, ComPtr<ISlangBlob>& cubin, std::string& diagnostics);

private:
    Result compile(ISlangBlob* ptx, ComPtr<ISlangBlob>& cubin, std::string& diagnostics);
    void store(ISlangBlob* key, ISlangBlob* cubin);
    DeviceImpl* m_device = nullptr;
    std::string m_archOption;
    std::string m_optimizationOption;
    unsigned int m_major = 0;
    unsigned int m_minor = 0;
    bool m_trace = false;
};

} // namespace rhi::cuda

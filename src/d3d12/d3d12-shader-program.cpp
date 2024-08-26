// d3d12-shader-program.cpp
#include "d3d12-shader-program.h"

namespace gfx
{
namespace d3d12
{

using namespace Slang;

Result ShaderProgramImpl::createShaderModule(
    slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    ShaderBinary shaderBin;
    shaderBin.stage = entryPointInfo->getStage();
    shaderBin.entryPointInfo = entryPointInfo;
    shaderBin.code.assign(
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()),
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()) + (size_t)kernelCode->getBufferSize());
    m_shaders.push_back(_Move(shaderBin));
    return SLANG_OK;
}

} // namespace d3d12
} // namespace gfx

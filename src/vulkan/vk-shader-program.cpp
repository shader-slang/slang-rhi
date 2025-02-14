#include "vk-shader-program.h"
#include "vk-shader-object-layout.h"
#include "vk-device.h"
#include "vk-util.h"

namespace rhi::vk {

ShaderProgramImpl::ShaderProgramImpl(DeviceImpl* device)
    : m_device(device)
{
    for (auto& shaderModule : m_modules)
        shaderModule = VK_NULL_HANDLE;
}

ShaderProgramImpl::~ShaderProgramImpl()
{
    for (auto shaderModule : m_modules)
    {
        if (shaderModule != VK_NULL_HANDLE)
        {
            m_device->m_api.vkDestroyShaderModule(m_device->m_api.m_device, shaderModule, nullptr);
        }
    }
}

void ShaderProgramImpl::comFree()
{
    m_device.breakStrongReference();
}

VkPipelineShaderStageCreateInfo ShaderProgramImpl::compileEntryPoint(
    const char* entryPointName,
    ISlangBlob* code,
    VkShaderStageFlagBits stage,
    VkShaderModule& outShaderModule
)
{
    // We need to make a copy of the code, since the Slang compiler
    // will free the memory after a compile request is closed.

    VkShaderModuleCreateInfo moduleCreateInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleCreateInfo.pCode = (uint32_t*)code->getBufferPointer();
    moduleCreateInfo.codeSize = code->getBufferSize();

    VkShaderModule module;
    SLANG_VK_CHECK(m_device->m_api.vkCreateShaderModule(m_device->m_device, &moduleCreateInfo, nullptr, &module));
    outShaderModule = module;

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStageCreateInfo.stage = stage;

    shaderStageCreateInfo.module = module;
    shaderStageCreateInfo.pName = entryPointName;

    return shaderStageCreateInfo;
}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    m_codeBlobs.push_back(kernelCode);
    VkShaderModule shaderModule;
    auto realEntryPointName = entryPointInfo->getNameOverride();
    const char* spirvBinaryEntryPointName = "main";
    m_stageCreateInfos.push_back(compileEntryPoint(
        spirvBinaryEntryPointName,
        kernelCode,
        (VkShaderStageFlagBits)VulkanUtil::getShaderStage(entryPointInfo->getStage()),
        shaderModule
    ));
    m_entryPointNames.push_back(realEntryPointName);
    m_modules.push_back(shaderModule);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootShaderObjectLayout;
}


} // namespace rhi::vk

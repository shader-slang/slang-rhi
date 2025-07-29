#include "vk-shader-program.h"
#include "vk-shader-object-layout.h"
#include "vk-device.h"
#include "vk-utils.h"

namespace rhi::vk {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    for (const auto& module : m_modules)
    {
        if (module.shaderModule != VK_NULL_HANDLE)
        {
            device->m_api.vkDestroyShaderModule(device->m_device, module.shaderModule, nullptr);
        }
    }
}

// Scan SPIR-V code to find the bindless descriptor set.
// Returns descriptorSet index -1 if not found.
inline Result findBindlessDescriptorSet(const void* codeData, size_t codeSize, uint32_t& outDescriptorSet)
{
    const uint32_t* word = (const uint32_t*)codeData;
    size_t wordsLeft = codeSize / sizeof(uint32_t);

    struct HeapInfo
    {
        uint32_t id;
        uint32_t binding = uint32_t(-1);
        uint32_t descriptorSet = uint32_t(-1);
    };

    static_vector<HeapInfo, 32> infos;

    static constexpr char kName[24] = "__slang_resource_heap\0\0";
    static_assert(sizeof(kName) % 4 == 0, "Name must be 4-byte aligned");
    static_assert(kName[sizeof(kName) - 1] == 0, "Name must be null-terminated");
    static constexpr size_t kNameWords = sizeof(kName) / sizeof(uint32_t);

#define ADVANCE(n)                                                                                                     \
    word += n;                                                                                                         \
    wordsLeft -= n;

    // Process SPIRV header
    if (wordsLeft < 5 || word[0] != 0x07230203)
    {
        return SLANG_FAIL;
    }
    ADVANCE(5);

    // Process opcodes
    while (wordsLeft > 0)
    {
        uint32_t opcode = *word & 0xFFFF;
        uint32_t wordCount = *word >> 16;

        if (wordCount > wordsLeft)
        {
            return SLANG_FAIL;
        }

        if (opcode == 5 && wordCount == 2 + kNameWords) // OpName
        {
            // Check if the name is "__slang_resource_heap" and if so, store the ID
            if (::memcmp(word + 2, kName, sizeof(kName)) == 0)
            {
                infos.push_back({word[1]});
            }
        }
        if (opcode == 71 && wordCount == 4) // OpDecorate
        {
            for (auto& info : infos)
            {
                if (info.id == word[1])
                {
                    if (word[2] == 33) // Binding
                    {
                        info.binding = word[3];
                    }
                    else if (word[2] == 34) // DescriptorSet
                    {
                        info.descriptorSet = word[3];
                    }
                }
            }
        }

        ADVANCE(wordCount);
    }

#undef ADVANCE

    uint32_t descriptorSet = uint32_t(-1);

    // Find common descriptor set index.
    if (infos.size() > 0)
    {
        descriptorSet = infos[0].descriptorSet;
        for (size_t i = 1; i < infos.size(); ++i)
        {
            if (infos[i].descriptorSet != descriptorSet)
            {
                return SLANG_FAIL;
            }
        }
    }

    outDescriptorSet = descriptorSet;

    return SLANG_OK;
}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    m_modules.push_back({});
    auto& module = m_modules.back();
    m_stageCreateInfos.push_back({});
    auto& stageCreateInfo = m_stageCreateInfos.back();

    module.code = kernelCode;
    module.entryPointName = entryPointInfo->getNameOverride();
    SLANG_RETURN_ON_FAIL(findBindlessDescriptorSet(
        kernelCode->getBufferPointer(),
        kernelCode->getBufferSize(),
        module.bindlessDescriptorSet
    ));
    module.hasBindlessDescriptorSet = module.bindlessDescriptorSet != uint32_t(-1);

    VkShaderModuleCreateInfo moduleCreateInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleCreateInfo.pCode = (uint32_t*)module.code->getBufferPointer();
    moduleCreateInfo.codeSize = module.code->getBufferSize();
    SLANG_VK_RETURN_ON_FAIL(
        device->m_api.vkCreateShaderModule(device->m_device, &moduleCreateInfo, nullptr, &module.shaderModule)
    );

    stageCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageCreateInfo.stage = (VkShaderStageFlagBits)translateShaderStage(entryPointInfo->getStage());
    stageCreateInfo.module = module.shaderModule;
    stageCreateInfo.pName = "main";

    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootShaderObjectLayout;
}


} // namespace rhi::vk

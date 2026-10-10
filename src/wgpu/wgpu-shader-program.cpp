#include "wgpu-shader-program.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-device.h"
#include "core/blob.h"
#include "core/sha1.h"

namespace rhi::wgpu {

namespace {

// WebGPU cache entries contain both WGSL and post-codegen binding usage. Keep
// their keys separate from the code-only cache used by other backends.
constexpr uint32_t kShaderArtifactMagic = 0x55504757; // WGPU
constexpr uint32_t kShaderArtifactVersion = 1;

void appendUint32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        bytes.push_back(uint8_t(value >> (8 * i)));
}

uint32_t readUint32(const uint8_t*& bytes)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; ++i)
        value |= uint32_t(*bytes++) << (8 * i);
    return value;
}

bool decodeShaderArtifact(
    ISlangBlob* artifact,
    std::string_view& code,
    std::vector<ShaderProgramImpl::UsedBinding>& usedBindings
)
{
    if (!artifact || artifact->getBufferSize() < 16)
        return false;
    auto bytes = static_cast<const uint8_t*>(artifact->getBufferPointer());
    if (readUint32(bytes) != kShaderArtifactMagic || readUint32(bytes) != kShaderArtifactVersion)
        return false;
    uint32_t codeSize = readUint32(bytes);
    uint32_t bindingCount = readUint32(bytes);
    size_t remaining = artifact->getBufferSize() - 16;
    if (codeSize == 0 || codeSize > remaining || (remaining - codeSize) % 8 != 0 ||
        (remaining - codeSize) / 8 != bindingCount)
        return false;
    usedBindings.clear();
    usedBindings.reserve(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i)
    {
        uint32_t group = readUint32(bytes);
        uint32_t binding = readUint32(bytes);
        usedBindings.push_back({group, binding});
    }
    code = std::string_view(reinterpret_cast<const char*>(bytes), codeSize);
    return true;
}

ComPtr<ISlangBlob> encodeShaderArtifact(ISlangBlob* code, std::span<const ShaderProgramImpl::UsedBinding> usedBindings)
{
    if (code->getBufferSize() > UINT32_MAX || usedBindings.size() > UINT32_MAX)
        return nullptr;
    std::vector<uint8_t> bytes;
    appendUint32(bytes, kShaderArtifactMagic);
    appendUint32(bytes, kShaderArtifactVersion);
    appendUint32(bytes, uint32_t(code->getBufferSize()));
    appendUint32(bytes, uint32_t(usedBindings.size()));
    for (const auto& binding : usedBindings)
    {
        appendUint32(bytes, binding.group);
        appendUint32(bytes, binding.binding);
    }
    auto codeBytes = static_cast<const uint8_t*>(code->getBufferPointer());
    bytes.insert(bytes.end(), codeBytes, codeBytes + code->getBufferSize());
    return OwnedBlob::create(bytes.data(), bytes.size());
}

} // namespace

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    for (Module& module : m_modules)
    {
        device->m_ctx.api.wgpuShaderModuleRelease(module.module);
    }
}

Result ShaderProgramImpl::compileEntryPoint(Device* device, CompiledEntryPoint& entryPoint, bool measureCompilerTime)
{
    entryPoint.stats = {};
    entryPoint.stats.startTime = Timer::now();
    if (device->m_persistentShaderCache)
    {
        SHA1 hash("slang-rhi.wgpu.shader-artifact.v1");
        hash.update(entryPoint.cacheKey->getBufferPointer(), entryPoint.cacheKey->getBufferSize());
        auto digest = hash.getDigest();
        entryPoint.cacheKey = OwnedBlob::create(digest.data(), digest.size());

        ComPtr<ISlangBlob> cachedArtifact;
        if (SLANG_SUCCEEDED(
                device->m_persistentShaderCache->queryCache(entryPoint.cacheKey, cachedArtifact.writeRef())
            ))
        {
            std::string_view code;
            std::vector<UsedBinding> usedBindings;
            if (decodeShaderArtifact(cachedArtifact, code, usedBindings))
            {
                entryPoint.code = cachedArtifact;
                entryPoint.stats.endTime = Timer::now();
                entryPoint.stats.isCached = true;
                entryPoint.stats.cacheSize = cachedArtifact->getBufferSize();
                return SLANG_OK;
            }
        }
    }

    double startTotal = 0.0, startDownstream = 0.0, endTotal = 0.0, endDownstream = 0.0;
    auto globalSession = entryPoint.componentType->getSession()->getGlobalSession();
    if (measureCompilerTime)
        globalSession->getCompilerElapsedTime(&startTotal, &startDownstream);
    ComPtr<ISlangBlob> code;
    SLANG_RETURN_ON_FAIL(entryPoint.componentType->getEntryPointCode(
        entryPoint.entryPointIndex,
        entryPoint.targetIndex,
        code.writeRef(),
        entryPoint.diagnostics.writeRef()
    ));
    ComPtr<slang::IMetadata> metadata;
    ComPtr<ISlangBlob> diagnostics;
    Result result = entryPoint.componentType->getEntryPointMetadata(
        entryPoint.entryPointIndex,
        entryPoint.targetIndex,
        metadata.writeRef(),
        diagnostics.writeRef()
    );
    if (diagnostics)
    {
        std::string combined;
        if (entryPoint.diagnostics)
            combined = static_cast<const char*>(entryPoint.diagnostics->getBufferPointer());
        combined += static_cast<const char*>(diagnostics->getBufferPointer());
        entryPoint.diagnostics = OwnedBlob::create(combined.c_str(), combined.size() + 1);
    }
    SLANG_RETURN_ON_FAIL(result);

    // Only immutable reflection is read here: entry points may compile concurrently.
    std::vector<UsedBinding> usedBindings;
    const auto& groups = m_rootObjectLayout->m_bindGroupLayoutEntries;
    for (uint32_t group = 0; group < groups.size(); ++group)
    {
        for (const auto& binding : groups[group])
        {
            bool used = false;
            SLANG_RETURN_ON_FAIL(metadata->isParameterLocationUsed(
                SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT,
                group,
                binding.binding,
                used
            ));
            if (used)
                usedBindings.push_back({group, binding.binding});
        }
    }
    if (measureCompilerTime)
        globalSession->getCompilerElapsedTime(&endTotal, &endDownstream);

    entryPoint.code = encodeShaderArtifact(code, usedBindings);
    if (!entryPoint.code)
        return SLANG_FAIL;
    if (device->m_persistentShaderCache)
        device->m_persistentShaderCache->writeCache(entryPoint.cacheKey, entryPoint.code);
    entryPoint.stats.endTime = Timer::now();
    entryPoint.stats.totalTime = measureCompilerTime ? endTotal - startTotal : 0.0;
    entryPoint.stats.downstreamTime = measureCompilerTime ? endDownstream - startDownstream : 0.0;
    entryPoint.stats.cacheSize = entryPoint.code->getBufferSize();
    return SLANG_OK;
}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    auto existingError = device->getAndClearLastUncapturedError();
    if (existingError != WGPUErrorType_NoError)
        device->printWarning("Web GPU device had reported error before shader compilation.");

    Module module;
    module.stage = desc.stage;
    module.entryPointName = desc.entryPointName;
    std::string_view code;
    if (!decodeShaderArtifact(kernelCode, code, module.usedBindings))
        return SLANG_FAIL;
    module.code = code;

#if !SLANG_WASM
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code.data = module.code.c_str();
    wgslDesc.code.length = module.code.size();
#else
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{module.code.c_str(), module.code.size()};
#endif
    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = (WGPUChainedStruct*)&wgslDesc;

    module.module = device->m_ctx.api.wgpuDeviceCreateShaderModule(device->m_ctx.device, &moduleDesc);
    if (!module.module)
    {
        return SLANG_FAIL;
    }

    if (device->getAndClearLastUncapturedError() != WGPUErrorType_NoError)
    {
        return SLANG_FAIL;
    }

    m_modules.push_back(module);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

ShaderProgramImpl::Module* ShaderProgramImpl::findModule(SlangStage stage)
{
    for (Module& module : m_modules)
    {
        if (module.stage == stage)
            return &module;
    }
    return nullptr;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    if (shaderProgram->getSyntheticResourceBindingState())
        return SLANG_E_NOT_IMPLEMENTED;
    SLANG_RETURN_ON_FAIL(
        RootShaderObjectLayoutImpl::create(
            this,
            shaderProgram->linkedProgram,
            shaderProgram->linkedProgram->getLayout(),
            shaderProgram->m_rootObjectLayout.writeRef()
        )
    );
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

} // namespace rhi::wgpu

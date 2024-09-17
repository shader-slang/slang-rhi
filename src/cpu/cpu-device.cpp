#include "cpu-device.h"

#include "cpu-buffer.h"
#include "cpu-pipeline.h"
#include "cpu-query.h"
#include "cpu-shader-object.h"
#include "cpu-shader-program.h"
#include "cpu-texture.h"
#include "cpu-texture-view.h"

#include <chrono>

namespace rhi::cpu {

DeviceImpl::~DeviceImpl()
{
    m_currentPipeline = nullptr;
    m_currentRootObject = nullptr;
}

Result DeviceImpl::initialize(const Desc& desc)
{
    SLANG_RETURN_ON_FAIL(slangContext.initialize(
        desc.slang,
        desc.extendedDescCount,
        desc.extendedDescs,
        SLANG_SHADER_HOST_CALLABLE,
        "sm_5_1",
        make_array(slang::PreprocessorMacroDesc{"__CPU__", "1"})
    ));

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // Initialize DeviceInfo
    {
        m_info.deviceType = DeviceType::CPU;
        m_info.apiName = "CPU";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
        m_info.adapterName = "CPU";
        m_info.timestampFrequency = 1000000000;
    }

    // Can support pointers (or something akin to that)
    {
        m_features.push_back("has-ptr");
    }

    return SLANG_OK;
}

Result DeviceImpl::createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc srcDesc = fixupTextureDesc(desc);
    RefPtr<TextureImpl> texture = new TextureImpl(srcDesc);
    SLANG_RETURN_ON_FAIL(texture->init(initData));

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    auto desc = fixupBufferDesc(descIn);
    RefPtr<BufferImpl> buffer = new BufferImpl(desc);
    SLANG_RETURN_ON_FAIL(buffer->init());
    if (initData)
    {
        SLANG_RETURN_ON_FAIL(buffer->setData(0, desc.size, initData));
    }
    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(desc);
    view->m_texture = static_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Unknown)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayoutImpl> cpuLayout = new ShaderObjectLayoutImpl(this, session, typeLayout);
    returnRefPtrMove(outLayout, cpuLayout);

    return SLANG_OK;
}

Result DeviceImpl::createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    auto cpuLayout = static_cast<ShaderObjectLayoutImpl*>(layout);

    RefPtr<ShaderObjectImpl> result = new ShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, cpuLayout));
    returnComPtr(outObject, result);

    return SLANG_OK;
}

Result DeviceImpl::createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    auto cpuLayout = static_cast<ShaderObjectLayoutImpl*>(layout);

    RefPtr<MutableShaderObjectImpl> result = new MutableShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, cpuLayout));
    returnComPtr(outObject, result);

    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject)
{
    auto cpuProgram = static_cast<ShaderProgramImpl*>(program);
    auto cpuProgramLayout = cpuProgram->layout;

    RefPtr<RootShaderObjectImpl> result = new RootShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, cpuProgramLayout));
    returnRefPtrMove(outObject, result);
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> cpuProgram = new ShaderProgramImpl();
    cpuProgram->init(desc);
    auto slangGlobalScope = cpuProgram->linkedProgram;
    if (slangGlobalScope)
    {
        auto slangProgramLayout = slangGlobalScope->getLayout();
        if (!slangProgramLayout)
            return SLANG_FAIL;

        RefPtr<RootShaderObjectLayoutImpl> cpuProgramLayout =
            new RootShaderObjectLayoutImpl(this, slangGlobalScope->getSession(), slangProgramLayout);
        cpuProgramLayout->m_programLayout = slangProgramLayout;

        cpuProgram->layout = cpuProgramLayout;
    }

    returnComPtr(outProgram, cpuProgram);
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline)
{
    RefPtr<PipelineImpl> state = new PipelineImpl();
    state->init(desc);
    returnComPtr(outPipeline, state);
    return Result();
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> pool = new QueryPoolImpl();
    pool->init(desc);
    returnComPtr(outPool, pool);
    return SLANG_OK;
}

void DeviceImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    static_cast<QueryPoolImpl*>(pool)->m_queries[index] =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
}

Result DeviceImpl::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    SLANG_UNUSED(desc);
    *outSampler = nullptr;
    return SLANG_OK;
}

void* DeviceImpl::map(IBuffer* buffer, MapFlavor flavor)
{
    SLANG_UNUSED(flavor);
    auto bufferImpl = static_cast<BufferImpl*>(buffer);
    return bufferImpl->m_data;
}

void DeviceImpl::unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(offsetWritten);
    SLANG_UNUSED(sizeWritten);
}

void DeviceImpl::setPipeline(IPipeline* state)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
}

void DeviceImpl::bindRootShaderObject(IShaderObject* object)
{
    m_currentRootObject = static_cast<RootShaderObjectImpl*>(object);
}

void DeviceImpl::dispatchCompute(int x, int y, int z)
{
    int entryPointIndex = 0;
    int targetIndex = 0;

    // Specialize the compute kernel based on the shader object bindings.
    RefPtr<Pipeline> newPipeline;
    maybeSpecializePipeline(m_currentPipeline, m_currentRootObject, newPipeline);
    m_currentPipeline = static_cast<PipelineImpl*>(newPipeline.Ptr());

    auto program = m_currentPipeline->getProgram();
    auto entryPointLayout = m_currentRootObject->getLayout()->getEntryPoint(entryPointIndex);
    auto entryPointName = entryPointLayout->getEntryPointName();

    auto entryPointObject = m_currentRootObject->getEntryPoint(entryPointIndex);

    ComPtr<ISlangSharedLibrary> sharedLibrary;
    ComPtr<ISlangBlob> diagnostics;
    auto compileResult =
        program->slangGlobalScope
            ->getEntryPointHostCallable(entryPointIndex, targetIndex, sharedLibrary.writeRef(), diagnostics.writeRef());
    if (diagnostics)
    {
        getDebugCallback()->handleMessage(
            compileResult == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
            DebugMessageSource::Slang,
            (char*)diagnostics->getBufferPointer()
        );
    }
    if (SLANG_FAILED(compileResult))
        return;

    auto func = (slang_prelude::ComputeFunc)sharedLibrary->findSymbolAddressByName(entryPointName);

    slang_prelude::ComputeVaryingInput varyingInput;
    varyingInput.startGroupID.x = 0;
    varyingInput.startGroupID.y = 0;
    varyingInput.startGroupID.z = 0;
    varyingInput.endGroupID.x = x;
    varyingInput.endGroupID.y = y;
    varyingInput.endGroupID.z = z;

    auto globalParamsData = m_currentRootObject->getDataBuffer();
    auto entryPointParamsData = entryPointObject->getDataBuffer();
    func(&varyingInput, entryPointParamsData, globalParamsData);
}

void DeviceImpl::copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size)
{
    auto dstImpl = static_cast<BufferImpl*>(dst);
    auto srcImpl = static_cast<BufferImpl*>(src);
    memcpy((uint8_t*)dstImpl->m_data + dstOffset, (uint8_t*)srcImpl->m_data + srcOffset, size);
}

} // namespace rhi::cpu

namespace rhi {

Result createCPUDevice(const IDevice::Desc* desc, IDevice** outDevice)
{
    RefPtr<cpu::DeviceImpl> result = new cpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi

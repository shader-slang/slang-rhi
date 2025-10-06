#include "cpu-device.h"
#include "cpu-pipeline.h"
#include "cpu-query.h"
#include "cpu-shader-object.h"
#include "cpu-shader-program.h"
#include "cpu-texture.h"

namespace rhi::cpu {

DeviceImpl::~DeviceImpl() {}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // Initialize device info
    {
        m_info.deviceType = DeviceType::CPU;
        m_info.apiName = "CPU";
        m_info.adapterName = "CPU";
        m_info.adapterLUID = {};
        m_info.timestampFrequency = 1000000000;
    }

    // Initialize features & capabilities
    addFeature(Feature::SoftwareDevice);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::TimestampQuery);
    addFeature(Feature::Pointer);

    addCapability(Capability::cpp);

    // Initialize format support table
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        FormatSupport formatSupport = FormatSupport::None;
        if (_getFormatInfo(format))
        {
            formatSupport |= FormatSupport::CopySource;
            formatSupport |= FormatSupport::CopyDestination;
            formatSupport |= FormatSupport::Texture;
            formatSupport |= FormatSupport::ShaderLoad;
            formatSupport |= FormatSupport::ShaderSample;
            formatSupport |= FormatSupport::ShaderUavLoad;
            formatSupport |= FormatSupport::ShaderUavStore;
            formatSupport |= FormatSupport::ShaderAtomic;
        }
        m_formatSupport[formatIndex] = formatSupport;
    }

    // Initialize slang context
    SLANG_RETURN_ON_FAIL(m_slangContext.initialize(
        desc.slang,
        SLANG_SHADER_HOST_CALLABLE,
        "sm_5_1",
        std::array{slang::PreprocessorMacroDesc{"__CPU__", "1"}}
    ));

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->setInternalReferenceCount(1);

    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = 1;
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

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> program = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(program->init());
    auto slangGlobalScope = program->linkedProgram;
    if (slangGlobalScope)
    {
        auto slangProgramLayout = slangGlobalScope->getLayout();
        if (!slangProgramLayout)
            return SLANG_FAIL;

        RefPtr<RootShaderObjectLayoutImpl> rootShaderObjectLayout =
            new RootShaderObjectLayoutImpl(this, slangGlobalScope->getSession(), slangProgramLayout);
        rootShaderObjectLayout->m_programLayout = slangProgramLayout;

        program->m_rootShaderObjectLayout = rootShaderObjectLayout;
    }

    returnComPtr(outProgram, program);
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    SLANG_UNUSED(desc);
    *outSampler = nullptr;
    return SLANG_OK;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
    {
        return SLANG_FAIL;
    }
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

void DeviceImpl::customizeShaderObject(ShaderObject* shaderObject)
{
    shaderObject->m_setBindingHook = shaderObjectSetBinding;
}

} // namespace rhi::cpu

namespace rhi {

IAdapter* getCPUAdapter(uint32_t index)
{
    static Adapter adapter = []()
    {
        Adapter outAdapter;
        AdapterInfo info = {};
        info.deviceType = DeviceType::CPU;
        info.adapterType = AdapterType::Software;
        string::copy_safe(info.name, sizeof(info.name), "Default");
        outAdapter.m_info = info;
        outAdapter.m_isDefault = true;
        return outAdapter;
    }();
    return index == 0 ? &adapter : nullptr;
}

Result createCPUDevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<cpu::DeviceImpl> result = new cpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi

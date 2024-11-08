#include "cpu-device.h"
#include "cpu-pipeline.h"
#include "cpu-query.h"
#include "cpu-shader-object.h"
#include "cpu-shader-program.h"

#include <chrono>

namespace rhi::cpu {

DeviceImpl::~DeviceImpl() {}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(slangContext.initialize(
        desc.slang,
        desc.extendedDescCount,
        desc.extendedDescs,
        SLANG_SHADER_HOST_CALLABLE,
        "sm_5_1",
        std::array{slang::PreprocessorMacroDesc{"__CPU__", "1"}}
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

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);

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
    auto cpuLayout = checked_cast<ShaderObjectLayoutImpl*>(layout);

    RefPtr<ShaderObjectImpl> result = new ShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, cpuLayout));
    returnComPtr(outObject, result);

    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    auto cpuProgram = checked_cast<ShaderProgramImpl*>(program);
    auto cpuProgramLayout = cpuProgram->layout;

    RefPtr<RootShaderObjectImpl> result = new RootShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, cpuProgramLayout));
    returnComPtr(outObject, result);
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

} // namespace rhi::cpu

namespace rhi {

Result createCPUDevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<cpu::DeviceImpl> result = new cpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi

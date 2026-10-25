#include "metal-shader-program.h"
#include "metal-device.h"
#include "metal-shader-object-layout.h"
#include "metal-utils.h"

namespace rhi::metal {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    Module module;
    module.stage = desc.stage;
    module.entryPointName = desc.entryPointName;
    module.code = kernelCode;

    dispatch_data_t data = dispatch_data_create(
        kernelCode->getBufferPointer(),
        kernelCode->getBufferSize(),
        dispatch_get_main_queue(),
        NULL
    );
    NS::Error* error;
    module.library = NS::TransferPtr(device->m_device->newLibrary(data, &error));
    if (!module.library)
    {
        const char* msg = error->localizedDescription()->utf8String();
        device->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg);
        return SLANG_E_INVALID_ARG;
    }

    m_modules.push_back(module);
    return SLANG_OK;
}

Result ShaderProgramImpl::Module::createFunction(DeviceImpl* device, NS::SharedPtr<MTL::Function>& outFunction) const
{
    auto functionName = createString(entryPointName.data());
    NS::SharedPtr<MTL::Function> function = NS::TransferPtr(library->newFunction(functionName.get()));
    if (!function)
        return SLANG_FAIL;

    // Metal refuses to build a pipeline from a function that declares function constants until it
    // is specialized ("Use newFunctionWithName:constantValues:"), even when every constant has a
    // default.
    NS::Dictionary* constants = function->functionConstantsDictionary();
    if (constants && constants->count() > 0)
    {
        NS::SharedPtr<MTL::FunctionConstantValues> values =
            NS::TransferPtr(MTL::FunctionConstantValues::alloc()->init());
        NS::Error* error = nullptr;
        function = NS::TransferPtr(library->newFunction(functionName.get(), values.get(), &error));
        if (!function)
        {
            if (error)
            {
                device->handleMessage(
                    DebugMessageType::Error,
                    DebugMessageSource::Driver,
                    error->localizedDescription()->utf8String()
                );
            }
            return SLANG_FAIL;
        }
    }

    outFunction = function;
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::metal

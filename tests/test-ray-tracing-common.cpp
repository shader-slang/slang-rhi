#include "test-ray-tracing-common.h"

namespace rhi::testing {

Result loadShaderPrograms(
    IDevice* device,
    const char* moduleName,
    const std::vector<const char*>& programNames,
    IShaderProgram** outProgram
)
{
    ComPtr<slang::ISession> slangSession;
    slangSession = device->getSlangSession();

    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(moduleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);

    ComPtr<slang::IEntryPoint> entryPoint;

    for (const char* programName : programNames)
    {
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(programName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
    }

    ComPtr<slang::IComponentType> linkedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    SLANG_RETURN_ON_FAIL(result);

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    SLANG_RETURN_ON_FAIL(device->createShaderProgram(programDesc, outProgram));

    return SLANG_OK;
}

void launchPipeline(
    ICommandQueue* queue,
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IBuffer* resultBuffer,
    IAccelerationStructure* tlas
)
{
    auto commandEncoder = queue->createCommandEncoder();

    auto passEncoder = commandEncoder->beginRayTracingPass();
    auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
    auto cursor = ShaderCursor(rootObject);
    cursor["resultBuffer"].setBinding(resultBuffer);
    cursor["sceneBVH"].setBinding(tlas);
    passEncoder->dispatchRays(0, 1, 1, 1);
    passEncoder->end();

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();
}
} // namespace rhi::testing

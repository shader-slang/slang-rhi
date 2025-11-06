#include "test-ray-tracing-common.h"

namespace rhi::testing {

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

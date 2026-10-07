#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("example-path-tracer-scatter", ALL)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-example-path-tracer", "testScatter", program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);

    BufferDesc desc = {};
    desc.size = 74 * sizeof(float);
    desc.elementSize = sizeof(float);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto output = device->createBuffer(desc);
    REQUIRE(output);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["results"].setBinding(output));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    ComPtr<ICommandBuffer> commands;
    REQUIRE_CALL(encoder->finish(commands.writeRef()));
    REQUIRE_CALL(queue->submit(commands));
    REQUIRE_CALL(queue->waitOnHost());

    std::array<float, 74> expected;
    const std::array<float, 12> surface = {1, 1, 0, 0, 0, 1, 0.25f, 0.5f, 0.75f, 1, 1, 1};
    for (size_t i = 0; i < 6; ++i)
        std::copy(surface.begin(), surface.end(), expected.begin() + i * surface.size());
    expected[72] = expected[73] = 1;
    compareComputeResult(device, output, expected);
}

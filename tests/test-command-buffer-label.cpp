#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("command-buffer-set-label", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);

    // Set a label on a command buffer.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto commandBuffer = commandEncoder->finish();
        REQUIRE(commandBuffer);
        commandBuffer->setLabel("TestCommandBuffer");
    }

    // Set a label before submitting.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto commandBuffer = commandEncoder->finish();
        REQUIRE(commandBuffer);
        commandBuffer->setLabel("SubmittedCommandBuffer");
        queue->submit(commandBuffer);
        queue->waitOnHost();
    }

    // Set a label with nullptr (should not crash).
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto commandBuffer = commandEncoder->finish();
        REQUIRE(commandBuffer);
        commandBuffer->setLabel(nullptr);
    }
}

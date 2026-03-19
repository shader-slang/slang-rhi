#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("command-encoder-without-finish", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder.setNull();
}

GPU_TEST_CASE("command-encoder-labels", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);

    // Create command encoder with label.
    CommandEncoderDesc encoderDesc;
    encoderDesc.label = "test-encoder";
    auto encoder = queue->createCommandEncoder(encoderDesc);
    REQUIRE(encoder);

    // Verify encoder label via getDesc().
    const CommandEncoderDesc& retrievedEncoderDesc = encoder->getDesc();
    CHECK(retrievedEncoderDesc.label != nullptr);
    CHECK(std::strcmp(retrievedEncoderDesc.label, "test-encoder") == 0);

    // Finish with command buffer label.
    CommandBufferDesc bufferDesc;
    bufferDesc.label = "test-command-buffer";
    auto commandBuffer = encoder->finish(bufferDesc);
    REQUIRE(commandBuffer);

    // Verify command buffer label via getDesc().
    const CommandBufferDesc& retrievedBufferDesc = commandBuffer->getDesc();
    CHECK(retrievedBufferDesc.label != nullptr);
    CHECK(std::strcmp(retrievedBufferDesc.label, "test-command-buffer") == 0);
}

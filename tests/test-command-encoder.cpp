#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("command-encoder-without-finish", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder.setNull();
}

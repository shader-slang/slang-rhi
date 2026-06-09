#pragma once

#include <cstdint>
#include <string>

namespace rhi::testing::stress {

std::string makeLifetimeCanaryComputeShader();
std::string makeVariantComputeShader(uint32_t variant);
std::string makeSimpleRenderShader(uint32_t variant);

} // namespace rhi::testing::stress

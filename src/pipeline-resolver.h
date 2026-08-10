#pragma once

#include <slang-rhi.h>

namespace rhi {

class CommandList;
class Device;

/// Resolves virtual pipelines referenced by a command list.
Result resolvePipelines(Device* device, CommandList* commandList);

} // namespace rhi

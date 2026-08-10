#pragma once

#include <slang-rhi.h>

namespace rhi {

class CommandList;
class Device;

/// Resolves virtual pipelines referenced by a command list.
///
/// Slang front-end work, backend module installation, pipeline creation, and cache publication are
/// serialized. Fully prepared entry-point code generation may run concurrently.
Result resolvePipelines(Device* device, CommandList* commandList);

} // namespace rhi

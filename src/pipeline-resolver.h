#pragma once

#include <slang-rhi.h>

namespace rhi {

class CommandList;
class Device;

/// Resolves virtual pipelines referenced by a command list.
///
/// Front-end Slang work and cache publication are performed serially. Fully prepared
/// entry-point code generation and supported backend pipeline creation may run concurrently.
Result resolvePipelines(Device* device, CommandList* commandList);

} // namespace rhi

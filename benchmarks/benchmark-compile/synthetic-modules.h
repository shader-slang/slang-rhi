#pragma once

#include <slang.h>

#include <string>
#include <vector>

namespace rhi {

enum class SizeLevel
{
    Simple,
    Complex,
};

struct SyntheticModuleDesc
{
    std::string source;         // Slang source code
    std::string entryPointName; // Entry point name in this module
    SlangStage stage;           // SLANG_STAGE_RAY_GENERATION, _CLOSEST_HIT, or _MISS
};

struct SyntheticModuleParams
{
    int moduleCount = 8; // Number of closesthit modules
    SizeLevel sizeLevel = SizeLevel::Simple;
    int seed = 0; // Unique seed to defeat compilation caches
};

/// Generates a set of ray tracing modules for benchmarking.
/// Returns 1 raygen + 1 miss + moduleCount closesthit modules.
std::vector<SyntheticModuleDesc> generateSyntheticModules(const SyntheticModuleParams& params);

/// Returns a human-readable name for a size level ("simple", "complex").
const char* sizeLevelName(SizeLevel level);

} // namespace rhi

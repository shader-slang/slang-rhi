#pragma once

// Shared by the host and shaders. The recursive pipeline traces at most one ray
// per bounce, so its recursion depth must match this limit.
#define PATH_TRACER_MAX_BOUNCES 5
#define PATH_TRACER_SAMPLES_PER_PIXEL 4

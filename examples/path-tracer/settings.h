#pragma once

// Shared host/shader settings. Bounce count is adjustable at runtime; ray-pipeline
// recursion remains one because the integrator loops in the raygen shader.
#define PATH_TRACER_MAX_BOUNCES 12
#define PATH_TRACER_DEFAULT_BOUNCES 5
#define PATH_TRACER_SAMPLES_PER_PIXEL 4

#include "synthetic-modules.h"

#include <sstream>

namespace rhi {

const char* sizeLevelName(SizeLevel level)
{
    switch (level)
    {
    case SizeLevel::Simple:
        return "simple";
    case SizeLevel::Complex:
        return "complex";
    default:
        return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Shared payload struct injected into every module
// ---------------------------------------------------------------------------

static const char* kPayloadStruct =
    "[raypayload]\n"
    "struct RayPayload\n"
    "{\n"
    "    float4 color : read(caller, closesthit, miss) : write(caller, closesthit, miss);\n"
    "};\n\n";

// ---------------------------------------------------------------------------
// Raygen module (always small)
// ---------------------------------------------------------------------------

static SyntheticModuleDesc generateRaygenModule(int seed)
{
    std::string name = "rayGen_s" + std::to_string(seed);
    std::ostringstream ss;
    ss << kPayloadStruct;
    ss << "RaytracingAccelerationStructure accelStruct;\n\n";
    ss << "[shader(\"raygeneration\")]\n";
    ss << "void " << name << "()\n";
    ss << "{\n";
    ss << "    RayDesc ray;\n";
    ss << "    ray.Origin = float3(0, 0, -1);\n";
    ss << "    ray.Direction = float3(0, 0, 1);\n";
    ss << "    ray.TMin = 0.001;\n";
    ss << "    ray.TMax = 1000.0;\n";
    ss << "    RayPayload payload = { float4(0, 0, 0, 0) };\n";
    ss << "    TraceRay(accelStruct, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);\n";
    ss << "}\n";

    return SyntheticModuleDesc{ss.str(), std::move(name), SLANG_STAGE_RAY_GENERATION};
}

// ---------------------------------------------------------------------------
// Miss module (always small)
// ---------------------------------------------------------------------------

static SyntheticModuleDesc generateMissModule(int seed)
{
    std::string name = "missMain_s" + std::to_string(seed);
    std::ostringstream ss;
    ss << kPayloadStruct;
    ss << "[shader(\"miss\")]\n";
    ss << "void " << name << "(inout RayPayload payload)\n";
    ss << "{\n";
    ss << "    payload.color = float4(0.0, 0.0, 0.0, 1.0);\n";
    ss << "}\n";

    return SyntheticModuleDesc{ss.str(), std::move(name), SLANG_STAGE_MISS};
}

// ---------------------------------------------------------------------------
// Closesthit modules — size varies
// ---------------------------------------------------------------------------

/// Generate a Simple closesthit module (~20 lines).
static std::string generateClosestHitSimple(int index, int seed)
{
    std::ostringstream ss;
    ss << kPayloadStruct;
    ss << "[shader(\"closesthit\")]\n";
    ss << "void closestHit_" << index << "_s" << seed
       << "(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)\n";
    ss << "{\n";
    ss << "    payload.color = float4(attribs.barycentrics, 0.0, 1.0);\n";
    ss << "}\n";
    return ss.str();
}

/// Generate a Complex closesthit module (~5000+ lines).
/// Many layers of helper functions with heavy computation, deep call chains,
/// and abundant local variables to stress the compiler.
static std::string generateClosestHitComplex(int index, int seed)
{
    const int numLayers = 2;
    const int functionsPerLayer = 9;

    std::ostringstream ss;
    ss << kPayloadStruct;
    ss << "\n";

    // Generate layers of helper functions.
    // Each function has ~50 lines: 10 local variables, 2 loop blocks, inter-layer calls.
    for (int layer = 0; layer < numLayers; ++layer)
    {
        for (int f = 0; f < functionsPerLayer; ++f)
        {
            ss << "float3 layer" << layer << "_func" << f << "_" << index << "_s" << seed
               << "(float3 a, float3 b, float3 c)\n";
            ss << "{\n";

            // 10 local variables with varied initialization.
            ss << "    float3 t0 = a * b + c;\n";
            ss << "    float3 t1 = cross(a, b) + c * 0.5;\n";
            ss << "    float3 t2 = normalize(t0 + t1 + float3(0.001, 0.001, 0.001));\n";
            ss << "    float3 t3 = t0 * t1 + t2;\n";
            ss << "    float3 t4 = lerp(t0, t3, 0.3);\n";
            ss << "    float3 t5 = cross(t1, t2) + t0 * 0.7;\n";
            ss << "    float3 t6 = lerp(t3, t4, 0.4) + t5;\n";
            ss << "    float3 t7 = normalize(t5 + t6 + float3(0.001, 0.001, 0.001));\n";
            ss << "    float3 t8 = t0 * t7 + cross(t2, t6);\n";
            ss << "    float3 t9 = lerp(t4, t8, 0.6);\n";

            // First computation loop.
            int loopCount1 = 4 + layer + f;
            ss << "    for (int i = 0; i < " << loopCount1 << "; i++)\n";
            ss << "    {\n";
            ss << "        t0 = t0 * t2 + t4;\n";
            ss << "        t1 = cross(t0, t3) + t5 * 0.5;\n";
            ss << "        t2 = normalize(t1 + t0 + float3(0.001, 0.001, 0.001));\n";
            ss << "        t3 = lerp(t0, t1, float(i) / " << loopCount1 << ".0);\n";
            ss << "        t4 = t2 * t3 + t0;\n";
            ss << "        t5 = cross(t4, t6) + t7;\n";
            ss << "        t6 = lerp(t5, t8, float(i) / " << loopCount1 << ".0);\n";
            ss << "        t7 = normalize(t6 + t9 + float3(0.001, 0.001, 0.001));\n";
            ss << "        t8 = t7 * t0 + t1;\n";
            ss << "        t9 = cross(t8, t2) + t3;\n";
            ss << "    }\n";

            // Second computation loop with different patterns.
            int loopCount2 = 3 + (layer * 2 + f) % 7;
            ss << "    for (int j = 0; j < " << loopCount2 << "; j++)\n";
            ss << "    {\n";
            ss << "        float3 u = lerp(t0, t9, float(j) / " << loopCount2 << ".0);\n";
            ss << "        float3 v = cross(u, t5) + t3;\n";
            ss << "        t0 = normalize(u + v + float3(0.001, 0.001, 0.001));\n";
            ss << "        t1 = t0 * v + u;\n";
            ss << "        t4 = cross(t1, t7) + t6 * 0.3;\n";
            ss << "        t7 = lerp(t4, t9, 0.5);\n";
            ss << "        t9 = normalize(t7 + t1 + float3(0.001, 0.001, 0.001));\n";
            ss << "    }\n";

            // Call previous layer functions if available (creates deep call chains).
            if (layer > 0)
            {
                for (int pf = 0; pf < functionsPerLayer; ++pf)
                {
                    ss << "    t" << (pf % 10) << " = layer" << (layer - 1) << "_func" << pf << "_" << index << "_s"
                       << seed << "(t0, t1, t2);\n";
                }
            }

            // Additional cross-variable mixing after inter-layer calls.
            ss << "    t0 = lerp(t0, t5, 0.5) + cross(t1, t9);\n";
            ss << "    t3 = normalize(t0 + t3 + t6 + float3(0.001, 0.001, 0.001));\n";
            ss << "    t7 = t3 * t8 + t4;\n";

            ss << "    return normalize(t0 + t1 + t2 + t3 + t4 + t5 + t6 + t7 + t8 + t9 + float3(0.001, 0.001, "
                  "0.001));\n";
            ss << "}\n\n";
        }
    }

    // Entry point calling all top-layer functions plus some from middle layers.
    ss << "[shader(\"closesthit\")]\n";
    ss << "void closestHit_" << index << "_s" << seed
       << "(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)\n";
    ss << "{\n";
    ss << "    float3 result = float3(attribs.barycentrics, 0.0);\n";
    ss << "    float3 acc = float3(0, 0, 0);\n";

    int topLayer = numLayers - 1;
    for (int f = 0; f < functionsPerLayer; ++f)
    {
        ss << "    acc = acc + layer" << topLayer << "_func" << f << "_" << index << "_s" << seed << "(result, float3("
           << (f + 1) << ", " << (f + 2) << ", " << (f + 3) << "), acc);\n";
    }

    // Also call a few mid-layer functions to prevent dead-code elimination.
    int midLayer = numLayers / 2;
    for (int f = 0; f < functionsPerLayer; ++f)
    {
        ss << "    acc = acc + layer" << midLayer << "_func" << f << "_" << index << "_s" << seed
           << "(acc, result, float3(" << (f + 1) << ", 0, 0));\n";
    }

    ss << "    payload.color = float4(normalize(acc + float3(0.001, 0.001, 0.001)), 1.0);\n";
    ss << "}\n";

    return ss.str();
}

static std::string generateClosestHit(int index, SizeLevel sizeLevel, int seed)
{
    switch (sizeLevel)
    {
    case SizeLevel::Simple:
        return generateClosestHitSimple(index, seed);
    case SizeLevel::Complex:
        return generateClosestHitComplex(index, seed);
    default:
        return generateClosestHitSimple(index, seed);
    }
}

std::vector<SyntheticModuleDesc> generateSyntheticModules(const SyntheticModuleParams& params)
{
    std::vector<SyntheticModuleDesc> modules;
    modules.reserve(2 + params.moduleCount);

    // 1 raygen (always small).
    modules.push_back(generateRaygenModule(params.seed));

    // 1 miss (always small).
    modules.push_back(generateMissModule(params.seed));

    // N closesthit modules.
    for (int i = 0; i < params.moduleCount; ++i)
    {
        std::string entryPointName = "closestHit_" + std::to_string(i) + "_s" + std::to_string(params.seed);
        modules.push_back(
            SyntheticModuleDesc{
                generateClosestHit(i, params.sizeLevel, params.seed),
                std::move(entryPointName),
                SLANG_STAGE_CLOSEST_HIT,
            }
        );
    }

    return modules;
}

} // namespace rhi

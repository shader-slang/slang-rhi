#include "settings.h"

#ifndef NGP_USE_COOP
#define NGP_USE_COOP 0
#endif

struct Params
{
    uint step;
    uint seed;
    uint previewSize;
    uint gridEntryCount;
    uint4 matrixOffsets;
};
uniform Params params;

struct GridLevel
{
    uint resolution;
    uint offset;
    uint entryCount;
};

RWStructuredBuffer<float> weights;
RWStructuredBuffer<float2> moments;
// Each sample owns a column. No floating-point atomics or racing gradient writes.
RWStructuredBuffer<float> gradients;
StructuredBuffer<GridLevel> gridLevels;
RWStructuredBuffer<float> gridFeatures;
RWStructuredBuffer<float2> gridMoments;
// Each sample writes four nodes per level. Integer atomics link contributions
// sharing an entry; the optimizer sums them after the training dispatch ends.
RWStructuredBuffer<Atomic<uint>> gridHeads;
RWStructuredBuffer<uint> gridNext;
RWStructuredBuffer<float2> gridGradients;
RWByteAddressBuffer packedWeights;
// Matrix loads in DirectX LinAlg require an SRV. Bind the same packed buffer
// separately for reading during backpropagation and writing during packing.
ByteAddressBuffer packedWeightsRead;
ByteAddressBuffer optimalWeights;
RWStructuredBuffer<float> validation;
[format("rgba32f")] WTexture2D<float4> preview;

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

float random(uint x) { return float(hash(x) >> 8) / 16777216.0f; }

float noise(float2 p)
{
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    uint s = uint(cell.x) * 1597334677u + uint(cell.y) * 3812015801u + params.seed;
    return lerp(lerp(random(s), random(s + 1597334677u), f.x),
                lerp(random(s + 3812015801u), random(s + 1597334677u + 3812015801u), f.x), f.y);
}

// A stationary, bounded-frequency target. Only the network is differentiated.
float3 marble(float2 uv)
{
    float frequency = lerp(1.6f, 2.8f, random(params.seed + 19));
    float turbulence = lerp(0.45f, 0.9f, random(params.seed + 73));
    float warp = noise(uv * 2.0f) + 0.3f * noise(uv * 4.0f) + 0.08f * noise(uv * 8.0f);
    float phase = 6.2831853f * (frequency * (uv.x + 0.32f * uv.y) + turbulence * warp);
    float band = 0.5f + 0.5f * sin(phase);
    float cloud = smoothstep(0.15f, 0.9f, noise(uv * 3.0f + 7.0f));
    float3 navy = lerp(float3(0.012f, 0.028f, 0.075f), float3(0.08f, 0.20f, 0.29f), cloud);
    float3 color = lerp(navy, float3(0.62f, 0.76f, 0.80f), pow(band, 3.0f) * 0.8f);
    float veinDistance = (band - 0.36f) / 0.075f;
    float vein = exp(-veinDistance * veinDistance);
    return lerp(color, float3(0.83f, 0.52f, 0.13f), vein * 0.85f);
}

struct Vector<int N> : IDifferentiable
{
    float data[N];
};

uint gridEntry(uint2 vertex, GridLevel level)
{
    uint side = level.resolution + 1;
    uint index = side * side <= level.entryCount
        ? vertex.y * side + vertex.x
        : (vertex.x ^ (vertex.y * 2654435761u)) % level.entryCount;
    return level.offset + index;
}

[BackwardDerivative(loadGridBwd)]
float2 loadGrid(no_diff uint entry, no_diff uint node)
{
    return float2(gridFeatures[2 * entry], gridFeatures[2 * entry + 1]);
}

void loadGridBwd(uint entry, uint node, float2 gradient)
{
    gridGradients[node] = gradient;
    // Zero is the empty-list sentinel; links encode node + 1.
    gridNext[node] = gridHeads[entry].exchange(node + 1);
}

[Differentiable]
Vector<NGP_INPUTS> encode(no_diff float2 uv, no_diff uint sample)
{
    Vector<NGP_INPUTS> result;
    [MaxIters(NGP_GRID_LEVELS)]
    for (uint i = 0; i < NGP_GRID_LEVELS; ++i)
    {
        let level = no_diff gridLevels[i];
        float2 position = saturate(uv) * float(level.resolution);
        uint2 cell = min(uint2(position), uint2(level.resolution - 1));
        float2 f = position - float2(cell);
        uint node = (sample * NGP_GRID_LEVELS + i) * 4;
        // Keep the four corners explicit: Slang 2026.17.1 transposes the saved
        // array dimensions when differentiating nested level/corner loops.
        float2 a = loadGrid(no_diff gridEntry(cell, level), node);
        float2 b = loadGrid(no_diff gridEntry(cell + uint2(1, 0), level), node + 1);
        float2 c = loadGrid(no_diff gridEntry(cell + uint2(0, 1), level), node + 2);
        float2 d = loadGrid(no_diff gridEntry(cell + uint2(1, 1), level), node + 3);
        float2 value = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
        result.data[2 * i] = value.x;
        result.data[2 * i + 1] = value.y;
    }
    return result;
}

// The custom derivative connects autodiff to shared parameter storage. Coop vec
// accelerates both the forward multiply and propagation to the preceding layer.
[BackwardDerivative(linearBwd)]
Vector<O> linear<int I, int O>(Vector<I> x, no_diff uint w, no_diff uint b,
                              no_diff uint matrixOffset, no_diff uint sample)
{
    Vector<O> result;
#if NGP_USE_COOP
    CoopVec<half, I> input;
    for (int i = 0; i < I; ++i)
        input[i] = half(x.data[i]);
    let output = coopVecMatMul<half, O>(input, CoopVecComponentType.Float16,
        optimalWeights, matrixOffset, CoopVecComponentType.Float16,
        CoopVecMatrixLayout.InferencingOptimal, false, 0);
    for (int o = 0; o < O; ++o)
        result.data[o] = float(output[o]) + weights[b + o];
#else
    for (int o = 0; o < O; ++o)
    {
        float value = weights[b + o];
        for (int i = 0; i < I; ++i)
            value += weights[w + o * I + i] * x.data[i];
        result.data[o] = value;
    }
#endif
    return result;
}

void linearBwd<int I, int O>(inout DifferentialPair<Vector<I>> x, uint w, uint b,
                            uint matrixOffset, uint sample, Vector<O> dy)
{
    Vector<I> dx;
#if NGP_USE_COOP
    CoopVec<half, O> outputGradient;
    for (int o = 0; o < O; ++o)
        outputGradient[o] = half(dy.data[o]);
    // The row-major O x I matrix is the column-major I x O transpose.
    let inputGradient = coopVecMatMul<half, I>(outputGradient, CoopVecComponentType.Float16,
        packedWeightsRead, w * 2, CoopVecComponentType.Float16,
        CoopVecMatrixLayout.ColumnMajor, false, I * 2);
    for (int i = 0; i < I; ++i)
        dx.data[i] = float(inputGradient[i]);
#else
    for (int i = 0; i < I; ++i)
    {
        float value = 0;
        for (int o = 0; o < O; ++o)
            value += weights[w + o * I + i] * dy.data[o];
        dx.data[i] = value;
    }
#endif
    for (int o = 0; o < O; ++o)
    {
        gradients[(b + o) * NGP_BATCH_SIZE + sample] = dy.data[o];
        for (int i = 0; i < I; ++i)
            gradients[(w + o * I + i) * NGP_BATCH_SIZE + sample] = dy.data[o] * x.p.data[i];
    }
    x = diffPair(x.p, dx);
}

[Differentiable]
Vector<N> activate<int N>(Vector<N> x)
{
    Vector<N> result;
    [MaxIters(N)]
    for (int i = 0; i < N; ++i)
        result.data[i] = max(x.data[i], 0.01f * x.data[i]);
    return result;
}

[Differentiable]
float3 evaluate(no_diff float2 uv, no_diff uint sample)
{
    let input = encode(uv, sample);
    let a = activate(linear<NGP_INPUTS, NGP_WIDTH>(input, NGP_W0, NGP_B0, params.matrixOffsets.x, sample));
    let b = activate(linear<NGP_WIDTH, NGP_WIDTH>(a, NGP_W1, NGP_B1, params.matrixOffsets.y, sample));
    let c = linear<NGP_WIDTH, NGP_OUTPUTS>(b, NGP_W2, NGP_B2, params.matrixOffsets.z, sample);
    return float3(c.data[0], c.data[1], c.data[2]);
}

[Differentiable]
float loss(no_diff float2 uv, no_diff uint sample)
{
    float3 error = evaluate(uv, sample) - no_diff marble(uv);
    return dot(error, error) / 3.0f;
}

// Each thread differentiates one sample's loss through the MLP and grid encoding.
// linearBwd writes per-sample MLP gradients; loadGridBwd links grid contributions
// by entry. After this dispatch, optimize and optimizeGrid reduce those gradients
// and apply Adam to the MLP weights and grid features, respectively.
[shader("compute")]
[numthreads(32, 1, 1)]
void train(uint3 tid : SV_DispatchThreadID)
{
    uint sample = tid.x;
    if (sample >= NGP_BATCH_SIZE) return;
    uint key = params.step * NGP_BATCH_SIZE + sample;
    float2 uv = float2(random(2 * key + 123), random(2 * key + 124));
    bwd_diff(loss)(uv, sample, 1.0f);
}

float adam(float gradient, inout float2 state)
{
    gradient = clamp(gradient / NGP_BATCH_SIZE, -1.0f, 1.0f);
    state.x = lerp(gradient, state.x, 0.9f);
    state.y = lerp(gradient * gradient, state.y, 0.999f);
    float m = state.x / (1.0f - pow(0.9f, float(params.step + 1)));
    float v = state.y / (1.0f - pow(0.999f, float(params.step + 1)));
    return m / (sqrt(v) + 1e-8f);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void optimize(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x;
    if (index >= NGP_PARAMETER_COUNT) return;
    float gradient = 0;
    for (uint sample = 0; sample < NGP_BATCH_SIZE; ++sample)
        gradient += gradients[index * NGP_BATCH_SIZE + sample];
    float2 state = moments[index];
    weights[index] -= NGP_LEARNING_RATE * adam(gradient, state);
    moments[index] = state;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void optimizeGrid(uint3 tid : SV_DispatchThreadID)
{
    uint entry = tid.x;
    if (entry >= params.gridEntryCount) return;
    float2 gradient = float2(0);
    uint link = gridHeads[entry].load();
    while (link != 0)
    {
        uint node = link - 1;
        gradient += gridGradients[node];
        link = gridNext[node];
    }
    // Update untouched entries too, so Adam's moment decay uses the same global
    // step as the MLP. There is no dense per-sample grid-gradient allocation.
    for (uint feature = 0; feature < NGP_GRID_FEATURES; ++feature)
    {
        uint index = entry * NGP_GRID_FEATURES + feature;
        float2 state = gridMoments[index];
        gridFeatures[index] -= NGP_GRID_LEARNING_RATE * adam(gradient[feature], state);
        gridMoments[index] = state;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void pack(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x * 2;
    if (index >= NGP_PARAMETER_COUNT) return;
    packedWeights.Store(index * 2, f32tof16(weights[index]) | (f32tof16(weights[index + 1]) << 16));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void validate(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= NGP_VALIDATION_SIZE)) return;
    float2 uv = (float2(tid.xy) + 0.5f) / NGP_VALIDATION_SIZE;
    validation[tid.y * NGP_VALIDATION_SIZE + tid.x] = loss(uv, 0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void infer(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= params.previewSize)) return;
    float2 uv = (float2(tid.xy) + 0.5f) / params.previewSize;
    float3 reference = marble(uv);
    float3 learned = evaluate(uv, 0);
    float error = sqrt(dot(learned - reference, learned - reference) / 3.0f);
    preview.Store(tid.xy, float4(reference, 1));
    preview.Store(tid.xy + uint2(params.previewSize, 0), float4(saturate(learned), 1));
    preview.Store(tid.xy + uint2(2 * params.previewSize, 0), float4(error, 0, 0, 1));
}

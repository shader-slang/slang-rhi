uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<float> callableResults;
uniform uint nestedIndex;

struct CallableNode { float3 value; uint tag; }
struct CallableData
{
    uint seed;
    CallableNode nodes[2];
    float4 weights;
    row_major float2x3 rows;
    column_major float3x1 column;
    half2 small;
    double wide;
    bool flag;
    int64_t big;
};

void initialize(out CallableData p, uint seed)
{
    p.seed = seed;
    for (uint i = 0; i < 2; ++i)
    {
        p.nodes[i].value = float3(seed + 1 + i * 4, seed + 2 + i * 4, seed + 3 + i * 4);
        p.nodes[i].tag = seed + 4 + i * 4;
    }
    p.weights = float4(seed + 9, seed + 10, seed + 11, seed + 12);
    for (uint r = 0; r < 2; ++r)
        for (uint c = 0; c < 3; ++c)
            p.rows[r][c] = seed + 13 + r * 3 + c;
    for (uint r = 0; r < 3; ++r)
        p.column[r][0] = seed + 19 + r;
    p.small = half2(half(seed + 22), half(seed + 23));
    p.wide = double(seed + 24);
    p.flag = (seed & 1) != 0;
    p.big = (int64_t(1) << 40) + int64_t(seed + 26);
}

void addValues(inout CallableData p, uint delta)
{
    p.seed += delta;
    for (uint i = 0; i < 2; ++i)
    {
        p.nodes[i].value += float(delta);
        p.nodes[i].tag += delta;
    }
    p.weights += float(delta);
    for (uint r = 0; r < 2; ++r)
        for (uint c = 0; c < 3; ++c)
            p.rows[r][c] += float(delta);
    for (uint r = 0; r < 3; ++r)
        p.column[r][0] += float(delta);
    p.small += half(delta);
    p.wide += double(delta);
    p.flag = !p.flag;
    p.big += int64_t(delta);
}

[shader("callable")]
void callableAdd(inout CallableData p) { addValues(p, 10); }

[shader("callable")]
void callableChain(inout CallableData p)
{
    addValues(p, 20);
    CallShader(nestedIndex, p);
}

void runCallable(uint lane, uint stage)
{
    CallableData p;
    initialize(p, stage * 100 + lane);
    CallShader(lane & 1, p);
    uint offset = 1 + (lane * 2 + (stage != 0 ? 1 : 0)) * 27;
    callableResults[offset++] = float(p.seed);
    for (uint i = 0; i < 2; ++i)
    {
        for (uint c = 0; c < 3; ++c)
            callableResults[offset++] = p.nodes[i].value[c];
        callableResults[offset++] = float(p.nodes[i].tag);
    }
    for (uint c = 0; c < 4; ++c)
        callableResults[offset++] = p.weights[c];
    for (uint r = 0; r < 2; ++r)
        for (uint c = 0; c < 3; ++c)
            callableResults[offset++] = p.rows[r][c];
    for (uint r = 0; r < 3; ++r)
        callableResults[offset++] = p.column[r][0];
    callableResults[offset++] = float(p.small.x);
    callableResults[offset++] = float(p.small.y);
    callableResults[offset++] = float(p.wide);
    callableResults[offset++] = p.flag ? 1.0 : 0.0;
    callableResults[offset++] = float(p.big - (int64_t(1) << 40));
}

struct Empty {}
struct NestedEmpty { Empty values[2]; }
[shader("callable")]
void callableEmpty(inout Empty p)
{
    callableResults[433 + DispatchRaysIndex().x * 3] = 71.0;
}
[shader("callable")]
void callableNestedEmpty(inout NestedEmpty p)
{
    callableResults[434 + DispatchRaysIndex().x * 3] = 83.0;
}
[shader("callable")]
void callableOut(out uint p) { p = 97; }

[shader("raygeneration")]
void raygenCallables()
{
    uint lane = DispatchRaysIndex().x;
    runCallable(lane, 0);
    uint rayPayload = lane;
    RayDesc ray;
    ray.Origin = float3((lane & 1) == 0 ? 0.25 : 10.0, 0.25, 0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 100;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0u, 0, 0, 0, ray, rayPayload);
    Empty empty = {};
    CallShader(2, empty);
    NestedEmpty nested = {};
    CallShader(3, nested);
    uint outValue = 19;
    CallShader(4, outValue);
    callableResults[435 + lane * 3] = float(outValue);
}
[shader("closesthit")]
void hitCallables(inout uint lane, BuiltInTriangleIntersectionAttributes attributes)
{
    runCallable(lane, 1);
}
[shader("miss")]
void missCallables(inout uint lane) { runCallable(lane, 2); }

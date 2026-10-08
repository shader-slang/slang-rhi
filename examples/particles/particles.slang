struct Particle
{
    float4 positionAge;
    float4 velocityLife;
    float4 colorSize;
};

struct SimulationParams
{
    uint capacity;
    uint spawnCount;
    uint tick;
    float dt;
    float2 mouse;
    float mouseForce;
    float padding;
};
SimulationParams sim;
StructuredBuffer<Particle> inputParticles;
StructuredBuffer<uint> inputCount;
RWStructuredBuffer<Particle> outputParticles;
RWStructuredBuffer<Atomic<uint>> outputCount;
RWByteAddressBuffer drawArguments;

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float random(uint x)
{
    return float(hash(x) >> 8) / 16777216.0;
}

[shader("compute")][numthreads(128, 1, 1)] void simulateMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= min(inputCount[0], sim.capacity))
        return;
    Particle p = inputParticles[tid.x];
    p.positionAge.w += sim.dt;
    if (p.positionAge.w >= p.velocityLife.w)
        return;
    float2 xy = p.positionAge.xy;
    float radius = max(length(xy), 0.15);
    float2 radial = xy / radius;
    float2 tangent = float2(-radial.y, radial.x);
    // Relax toward an inward spiral instead of continuously accelerating the
    // tangential velocity (which would fling the cloud out of the viewport).
    float speed = 1.5 + 0.25 * sin(p.positionAge.w * 2);
    float inward = clamp((radius - 0.55) * 0.5, -0.2, 0.38);
    float2 targetVelocity = tangent * speed - radial * inward;
    float2 force = (targetVelocity - p.velocityLife.xy) * 2 - radial * speed * speed / max(radius, 0.5);
    float2 delta = sim.mouse - xy;
    force += delta * (sim.mouseForce / (dot(delta, delta) + 0.35));
    p.velocityLife.xy = (p.velocityLife.xy + force * sim.dt) * exp(-0.3 * sim.dt);
    p.positionAge.xyz += p.velocityLife.xyz * sim.dt;
    uint index = outputCount[0].add(1);
    outputParticles[index] = p;
}

    [shader("compute")][numthreads(128, 1, 1)] void spawnMain(uint3 tid : SV_DispatchThreadID)
{
    // The survivor count stays unchanged throughout this dispatch. Finalization
    // publishes the new count in a separate pass, after all spawn writes finish.
    uint base = outputCount[0].load();
    if (tid.x >= min(sim.spawnCount, sim.capacity - base))
        return;
    uint seed = hash(sim.tick) ^ hash(tid.x + 12345);
    // Two rotating emitters form orange and cyan spiral arms.
    uint arm = tid.x & 1;
    float angle = sim.tick * (1.0 / 120.0) * -0.35 + arm * 3.14159265 + (random(seed) - 0.5) * 0.3;
    float radius = 2.25 + random(seed + 1) * 0.55;
    float2 direction = float2(cos(angle), sin(angle));
    Particle p;
    p.positionAge = float4(direction * radius, (random(seed + 2) - 0.5) * 0.5, 0);
    p.velocityLife = float4(float2(-direction.y, direction.x) * 1.5, 0, 3 + random(seed + 3) * 4);
    p.colorSize =
        float4(lerp(float3(1, 0.12, 0.025), float3(0.015, 0.65, 1), float(arm)), 0.006 + random(seed + 5) * 0.006);
    outputParticles[base + tid.x] = p;
}

[shader("compute")][numthreads(1, 1, 1)] void finalizeMain()
{
    uint count = min(outputCount[0].load() + sim.spawnCount, sim.capacity);
    outputCount[0].store(count);
    // IndirectDrawArguments: vertexCount, instanceCount, startVertex, startInstance.
    drawArguments.Store4(0, uint4(6, count, 0, 0));
}

StructuredBuffer<Particle> particles;
StructuredBuffer<uint> liveCount;
float aspect;
float3 logoColor;

struct SpriteVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")] SpriteVertex particleVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    SpriteVertex result;
    result.position = float4(2, 2, 0, 1);
    result.uv = 0;
    result.color = 0;
    // The same shader supports the capacity-sized direct-draw fallback.
    if (instance >= liveCount[0])
        return result;
    const float2 corners[6] =
        {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    Particle p = particles[instance];
    float2 corner = corners[vertex];
    float2 position = p.positionAge.xy + corner * p.colorSize.w;
    result.position = float4(position / float2(3.6 * aspect, 3.6), 0, 1);
    result.uv = corner;
    float fade = saturate(p.positionAge.w * 5) * saturate((p.velocityLife.w - p.positionAge.w) * 2);
    result.color = float4(p.colorSize.rgb, fade);
    return result;
}

    [shader("fragment")] float4 particleFragment(SpriteVertex input)
    : SV_Target
{
    float r2 = dot(input.uv, input.uv);
    float glow = exp(-4 * r2) * saturate(1 - r2);
    return float4(input.color.rgb * glow * input.color.a, 0);
}

[shader("vertex")] float4 logoVertex(float3 position : POSITION)
    : SV_Position
{
    return float4((position.xy - float2(0, 1.58)) * 0.8 / float2(3.6 * aspect, 3.6), 0.5, 1);
}

[shader("fragment")] float4 logoFragment()
    : SV_Target
{
    return float4(logoColor * 0.25, 1);
}

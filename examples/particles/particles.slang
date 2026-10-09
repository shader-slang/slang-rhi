#include "settings.h"

struct Particle
{
    float4 positionAge;
    float4 velocityLife;
    float4 colorSize;
    // XY: logo target, Z: stable style seed, W: logo material.
    float4 targetStyle;
};

struct SimulationParams
{
    uint capacity;
    uint spawnCount;
    uint tick;
    float dt;
    float2 mouse;
    float mouseForce;
    uint cycleTick;
    float2 mousePrevious;
    float2 mouseVelocity;
};
SimulationParams sim;
StructuredBuffer<Particle> inputParticles;
StructuredBuffer<uint> inputCount;
RWStructuredBuffer<Particle> outputParticles;
RWStructuredBuffer<Atomic<uint>> outputCount;
RWByteAddressBuffer drawArguments;
StructuredBuffer<float4> logoTargets;

float formation(float time)
{
    return time < 10 ? smoothstep(3, 7, time) : 0;
}

// Curl of smooth traveling scalar waves. Each octave is divergence free, so
// the field folds particles into streams instead of isolated sinks.
float2 curlFlow(float2 p, float time)
{
    float2 k0 = float2(1.7, 1.1);
    float2 k1 = float2(-2.3, 2.9);
    float2 k2 = float2(4.1, 3.7);
    float2 gradient = k0 * cos(dot(k0, p) + time * 0.7) + 0.45 * k1 * cos(dot(k1, p) - time * 0.9) +
                      0.16 * k2 * cos(dot(k2, p) + time * 1.3);
    return float2(gradient.y, -gradient.x) * 0.42;
}

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
    float time = sim.cycleTick * (1.0 / 120.0);
    float flowTime = sim.tick * (1.0 / 120.0);
    float gather = formation(time);
    float2 xy = p.positionAge.xy;
    float radius = max(length(xy), 0.15);
    float2 radial = xy / radius;
    float2 tangent = float2(-radial.y, radial.x);
    float speed = 1.2 + 0.35 * sin(flowTime * 0.8 + radius * 2);
    float inward = clamp((radius - 1.5) * 0.65, -0.5, 1.8);
    float2 targetVelocity = tangent * speed - radial * inward + curlFlow(xy, flowTime);
    float2 flowForce = (targetVelocity - p.velocityLife.xy) * 2.2;
    float2 target = p.targetStyle.xy;
    // Keep a little motion in the formed logo without losing its silhouette.
    target += 0.008 * float2(sin(time * 3 + p.targetStyle.z * 40), cos(time * 2 + p.targetStyle.z * 31));
    float2 gatherForce = (target - xy) * 28 - p.velocityLife.xy * 10;
    float2 force = lerp(flowForce, gatherForce, gather);
    // Let the outgoing burst travel before the flow catches it.
    if (time >= 10 && time < 11.5)
        force *= smoothstep(10, 11.5, time);
    if (sim.cycleTick == PARTICLE_BURST_TICK)
    {
        float angle = p.targetStyle.z * 6.2831853;
        float2 outward = xy + 0.35 * float2(cos(angle), sin(angle));
        outward /= max(length(outward), 0.01);
        p.velocityLife.xy += outward * (2.5 + 3 * p.targetStyle.z) + tangent * 1.3;
    }
    // Sweep a compact brush along the cursor path so fast motion does not
    // skip particles. Stationary cursors contribute no force.
    float2 mouseTravel = sim.mouse - sim.mousePrevious;
    float along = saturate(dot(xy - sim.mousePrevious, mouseTravel) / max(dot(mouseTravel, mouseTravel), 1e-6));
    float2 offset = xy - (sim.mousePrevious + along * mouseTravel);
    float distance = length(offset);
    float influence = 1 - smoothstep(0, 0.5, distance);
    float mouseSpeed = length(sim.mouseVelocity);
    force += influence * (sim.mouseVelocity * 10 + offset / max(distance, 0.05) * mouseSpeed * 3);
    float2 delta = sim.mouse - xy;
    force += delta * (sim.mouseForce / (dot(delta, delta) + 0.35));
    p.velocityLife.xy = (p.velocityLife.xy + force * sim.dt) * exp(-0.12 * sim.dt);
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
    float4 target = logoTargets[hash(seed + 7) % PARTICLE_LOGO_TARGET_COUNT];
    float time = sim.cycleTick * (1.0 / 120.0);
    float gather = formation(time);
    // Material-matched streams preserve the colors as the logo assembles.
    float arm = target.z;
    float angle = sim.tick * (1.0 / 120.0) * -0.55 + arm * 3.14159265 + (random(seed) - 0.5) * 0.4;
    float radius = 2.2 + random(seed + 1) * 0.5;
    float2 direction = float2(cos(angle), sin(angle));
    Particle p;
    p.positionAge = float4(lerp(direction * radius, target.xy, gather * gather), 0, 0);
    p.velocityLife = float4(float2(-direction.y, direction.x) * 1.5 * (1 - gather), 0, 3 + random(seed + 3) * 4);
    float style = random(seed + 4);
    p.colorSize = float4(lerp(float3(1, 0.12, 0.025), float3(0.015, 0.65, 1), arm), lerp(0.004, 0.012, style * style));
    p.targetStyle = float4(target.xy, style, arm);
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
float cycleTime;

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
    float speed = length(p.velocityLife.xy);
    float2 direction = speed > 0.001 ? p.velocityLife.xy / speed : float2(1, 0);
    float2 side = float2(-direction.y, direction.x);
    float width = p.colorSize.w;
    float streak = min(speed * lerp(0.012, 0.04, p.targetStyle.z), 0.22);
    // The head stays at the simulated position; velocity stretches the tail.
    float2 position =
        p.positionAge.xy + side * corner.y * width + direction * (corner.x * width + (corner.x - 1) * 0.5 * streak);
    result.position = float4(position / float2(3.6 * aspect, 3.6), 0, 1);
    result.uv = corner;
    float fade = saturate(p.positionAge.w * 5) * saturate((p.velocityLife.w - p.positionAge.w) * 2);
    float gather = formation(cycleTime);
    float settled = gather * (1 - smoothstep(0.03, 0.3, length(p.positionAge.xy - p.targetStyle.xy)));
    float leader = smoothstep(0.94, 1, p.targetStyle.z);
    float energy = lerp(0.65, 3.5, leader) * lerp(1, 0.16, settled);
    // Spreading the sprite into a streak should not wash the burst out to white.
    energy *= sqrt(width / (width + 0.45 * streak));
    float heat = saturate(speed * 0.035 + leader * 0.12);
    result.color = float4(lerp(p.colorSize.rgb, float3(1, 0.85, 0.65), heat) * energy, fade);
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
    // A faint silhouette anchors the flow; particles supply the formed logo.
    return float4(logoColor * lerp(0.012, 0.002, formation(cycleTime)), 1);
}

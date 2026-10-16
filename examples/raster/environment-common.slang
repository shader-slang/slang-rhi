// Shared by the visible background and the lighting prefilter. All values are
// linear HDR radiance; the environment represents distant illumination only.
float3 rotateEnvironment(float3 direction, float angle)
{
    float s = sin(angle), c = cos(angle);
    return float3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

float softbox(float3 direction, float3 center, float2 size)
{
    float3 forward = normalize(center);
    float3 right = normalize(cross(float3(0, 1, 0), forward));
    float3 up = cross(forward, right);
    float facing = dot(direction, forward);
    float2 p = abs(float2(dot(direction, right), dot(direction, up))) / max(facing, 0.001);
    float2 edge = 1 - smoothstep(size * 0.85, size, p);
    return facing > 0 ? edge.x * edge.y : 0;
}

float3 evaluateEnvironment(float3 direction, uint preset)
{
    float3 color;
    if (preset == 0)
    {
        color = lerp(float3(0.035, 0.04, 0.05), float3(0.20, 0.23, 0.30), saturate(direction.y * 0.5 + 0.5));
        color += float3(5.0, 4.5, 3.8) * softbox(direction, float3(-0.75, 0.25, 1), float2(0.45, 0.65));
        color += float3(2.0, 3.0, 4.5) * softbox(direction, float3(1, 0.5, 0.2), float2(0.22, 0.8));
        color += float3(3.0, 2.4, 1.8) * softbox(direction, float3(0.2, 0.9, -1), float2(0.7, 0.22));
    }
    else
    {
        float3 horizon = float3(0.75, 0.85, 1.0);
        float3 sky = lerp(horizon, float3(0.12, 0.32, 0.75), pow(saturate(direction.y), 0.45));
        float3 ground = lerp(float3(0.12, 0.10, 0.08), horizon, pow(saturate(1 + direction.y), 8));
        // The sun is evaluated as a shadowed directional light, so it is excluded
        // from both diffuse and specular IBL. The background adds a visible disk.
        color = direction.y >= 0 ? sky : ground;
    }
    return color;
}

float3 environmentFaceDirection(uint face, float2 uv)
{
    float2 p = uv * 2 - 1;
    switch (face)
    {
    case 0:
        return normalize(float3(1, -p.y, -p.x));
    case 1:
        return normalize(float3(-1, -p.y, p.x));
    case 2:
        return normalize(float3(p.x, 1, p.y));
    case 3:
        return normalize(float3(p.x, -1, -p.y));
    case 4:
        return normalize(float3(p.x, -p.y, 1));
    default:
        return normalize(float3(-p.x, -p.y, -1));
    }
}

#include "../examples/path-tracer/path-tracer-common.slang"

RWStructuredBuffer<float> results;

[shader("compute")]
[numthreads(1, 1, 1)]
void testScatter()
{
    float3 normals[] = {
        float3(1, 0, 0),
        float3(0, 1, 0),
        float3(0, 0, 1),
        float3(0, 0, -1),
        normalize(float3(1, 1, -1)),
        normalize(float3(-1, -1, 1)),
    };
    for (uint i = 0; i < 6; ++i)
    {
        float3 n = normals[i];
        Frame basis = Frame(n);
        uint offset = i * 12;
        results[offset + 0] = dot(basis.t, basis.t);
        results[offset + 1] = dot(basis.b, basis.b);
        results[offset + 2] = dot(basis.t, n);
        results[offset + 3] = dot(basis.b, n);
        results[offset + 4] = dot(basis.t, basis.b);
        results[offset + 5] = dot(cross(basis.t, basis.b), n);

        // The same albedo must give the same weight for every surface orientation.
        Path path = Path(uint2(0), Ray(float3(0), -n), RNG(123));
        Vertex vertex = {float3(0), n, float2(0)};
        MaterialDesc material = {float3(0.25, 0.5, 0.75)};
        scatter(path, vertex, material);
        results[offset + 6] = path.thp.x;
        results[offset + 7] = path.thp.y;
        results[offset + 8] = path.thp.z;
        results[offset + 9] = dot(path.ray.dir, path.ray.dir);
        results[offset + 10] = dot(path.ray.dir, n) >= 0 ? 1 : 0;
        results[offset + 11] = float(path.vertexIndex);
    }

    // A grazing incident ray must not turn the normal into the zero vector.
    Path grazing = Path(uint2(0), Ray(float3(0), float3(1, 0, 0)), RNG(123));
    scatter(grazing, Vertex(float3(0), float3(0, 1, 0), float2(0)), MaterialDesc(float3(1)));
    results[72] = dot(grazing.ray.dir, grazing.ray.dir);
    results[73] = grazing.ray.dir.y >= 0 ? 1 : 0;
}

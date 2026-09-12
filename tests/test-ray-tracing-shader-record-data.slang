// This file intentionally isolates the shader-record declaration from the broad ray-tracing
// intrinsic test module. A Vulkan shader-record block contributes to the linked entry-point ABI,
// so putting it beside unrelated entry points would make those tests depend on record data too.

struct Result
{
    float3 value;
};

uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<Result> resultBuffer;

[raypayload]
struct RayPayload
{
    float3 value : read(caller) : write(closesthit);
};

// The pipeline helper requires a hit group and miss shader even though this ray-generation shader
// only exercises a callable record.
[shader("closesthit")]
void closestHitNOP(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{}

[shader("miss")]
void missNOP(inout RayPayload payload)
{}

// Keeping a first callable record with a larger payload verifies both that the callable stride is
// computed from the largest record and that CallShader(1, ...) selects the second record.
[shader("callable")]
void callableWriteConstant(inout RayPayload payload)
{
    payload.value = float3(1.0, 2.0, 3.0);
}

[[vk::shader_record]]
cbuffer CallableShaderRecord
{
    uint callableRecordValue;
}

[shader("callable")]
void callableReadShaderRecord(inout RayPayload payload)
{
    payload.value = float3(callableRecordValue, 0.0, 0.0);
}

[shader("raygeneration")]
void rayGenShaderCallRecord1()
{
    RayPayload payload = {};
    CallShader(1, payload);
    resultBuffer[0].value = payload.value;
}

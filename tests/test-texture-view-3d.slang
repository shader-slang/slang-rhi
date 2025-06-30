// test-texture-view-3d.slang - Shaders used by the unit tests in test-texture-view-3d.cpp

// Simple shader to test that a RWTexture3D texture view has been bound correctly. If tex has been
// bound correctly, only the contents contained within the specified texture view will be copied into
// the output buffer. Outside the shader, the output buffer is compared to expected texture data for
// the sub region specified in the texture view. Use numthreads 1,1,1 so dispatch can just be set to a
// subRegions/textureView's dimensions for simplicity.

[shader("compute")]
[numthreads(1,1,1)]
void testRWTex3DViewFloat(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform RWTexture3D<float> tex,
    uniform RWStructuredBuffer<float> buffer)
{
    uint width;
    uint height;
    uint depth;
    tex.GetDimensions(width, height, depth);

    float val = tex[uint3(sv_dispatchThreadID.x, sv_dispatchThreadID.y, sv_dispatchThreadID.z)];
    uint  idx = sv_dispatchThreadID.x + sv_dispatchThreadID.y * width + sv_dispatchThreadID.z * width * height;

    buffer[idx] = val;
}

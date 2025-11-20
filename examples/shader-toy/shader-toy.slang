typealias vec2 = float2;
typealias vec3 = float3;
typealias vec4 = float4;

typealias mat2 = float2x2;
typealias mat3 = float3x3;
typealias mat4 = float4x4;

#define mix lerp

uniform vec3      iResolution;           // viewport resolution (in pixels)
uniform float     iTime;                 // shader playback time (in seconds)
uniform float     iTimeDelta;            // render time (in seconds)
uniform float     iFrameRate;            // shader frame rate
uniform int       iFrame;                // shader playback frame
// uniform float     iChannelTime[4];       // channel playback time (in seconds)
// uniform vec3      iChannelResolution[4]; // channel resolution (in pixels)
uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
// uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube
// uniform vec4      iDate;                 // (year, month, day, time in seconds)
// uniform float     iSampleRate;           // sound sample rate (i.e., 44100)

void mainImage(out vec4 fragColor, in vec2 fragCoord);

[shader("compute")]
[numthreads(16, 16, 1)]
void mainCompute(uint3 tid: SV_DispatchThreadID, uniform RWTexture2D<float4> texture)
{
    if (any(tid.xy >= uint2(iResolution.xy)))
        return;

    float4 fragColor;
    mainImage(fragColor, tid.xy);
    texture[uint2(tid.x, uint(iResolution.y) - tid.y - 1)] = fragColor;
}

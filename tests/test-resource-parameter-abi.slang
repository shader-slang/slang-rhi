// Every vector is immediately followed by a live scalar, including compact CUDA vector3 values.
// The host supplies independent lane values and checks all observations and untouched storage.
[noinline]
float sampleCombined1D(Sampler1D<float> texture, float u)
{
    return texture.SampleLevel(u, 0);
}

[noinline]
float sampleCombined1DArray(Sampler1DArray<float> texture, float2 uv)
{
    return texture.SampleLevel(uv, 0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(
    uniform int2 i2, uniform uint tailI2, ByteAddressBuffer rawRead,
    uniform int3 i3, uniform uint tailI3, RWByteAddressBuffer rawWrite,
    uniform int4 i4, uniform uint tailI4, StructuredBuffer<uint> structuredRead,
    uniform uint2 u2, uniform uint tailU2, RWStructuredBuffer<uint> structuredWrite,
    uniform uint3 u3, uniform uint tailU3, Texture2D<float4> sampled,
    uniform uint4 u4, uniform uint tailU4, SamplerState sampler,
    uniform float2 f2, uniform uint tailF2, [format("rgba32ui")] RWTexture2D<uint4> surface,
    uniform float3 f3, uniform uint tailF3, Sampler1D<float> combined1D,
    uniform float4 f4, uniform uint tailF4, Sampler1DArray<float> combined1DArray,
    RWStructuredBuffer<uint> output, uniform uint finalTail)
{
    uint word = 1;
    for (uint lane = 0; lane < 2; ++lane) output[word++] = asuint(i2[lane]);
    output[word++] = tailI2;
    for (uint lane = 0; lane < 3; ++lane) output[word++] = asuint(i3[lane]);
    output[word++] = tailI3;
    for (uint lane = 0; lane < 4; ++lane) output[word++] = asuint(i4[lane]);
    output[word++] = tailI4;
    for (uint lane = 0; lane < 2; ++lane) output[word++] = u2[lane];
    output[word++] = tailU2;
    for (uint lane = 0; lane < 3; ++lane) output[word++] = u3[lane];
    output[word++] = tailU3;
    for (uint lane = 0; lane < 4; ++lane) output[word++] = u4[lane];
    output[word++] = tailU4;
    for (uint lane = 0; lane < 2; ++lane) output[word++] = asuint(f2[lane]);
    output[word++] = tailF2;
    for (uint lane = 0; lane < 3; ++lane) output[word++] = asuint(f3[lane]);
    output[word++] = tailF3;
    for (uint lane = 0; lane < 4; ++lane) output[word++] = asuint(f4[lane]);
    output[word++] = tailF4;

    // Observe each distinct binding before mutating the two writable allocations.
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint index = lane < 2 ? lane + 1 : lane * 2;
        output[word++] = rawRead.Load(index * 4);
    }
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint index = lane < 2 ? lane + 1 : lane * 2;
        output[word++] = rawWrite.Load(index * 4);
    }
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint index = lane < 2 ? lane + 1 : lane * 2;
        output[word++] = structuredRead[index];
    }
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint index = lane < 2 ? lane + 1 : lane * 2;
        output[word++] = structuredWrite[index];
    }
    uint count, stride;
    rawRead.GetDimensions(count);
    output[word++] = count;
    rawWrite.GetDimensions(count);
    output[word++] = count;
    structuredRead.GetDimensions(count, stride);
    output[word++] = count;
    output[word++] = stride;
    structuredWrite.GetDimensions(count, stride);
    output[word++] = count;
    output[word++] = stride;

    float4 sampledValue = sampled.SampleLevel(sampler, float2(0.5, 0.25), 0);
    uint4 prior = surface[uint2(1, 0)];
    for (uint lane = 0; lane < 4; ++lane) output[word++] = asuint(sampledValue[lane]);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = prior[lane];
    output[word++] = asuint(sampleCombined1D(combined1D, 0.125));
    output[word++] = asuint(sampleCombined1D(combined1D, 0.625));
    output[word++] = asuint(sampleCombined1DArray(combined1DArray, float2(0.125, 0)));
    output[word++] = asuint(sampleCombined1DArray(combined1DArray, float2(0.625, 0)));
    output[word++] = asuint(sampleCombined1DArray(combined1DArray, float2(0.125, 1)));
    output[word++] = asuint(sampleCombined1DArray(combined1DArray, float2(0.625, 1)));
    output[word++] = finalTail;

    for (uint index = 2; index < 6; ++index)
    {
        rawWrite.Store(index * 4, 0xb1000000 + index);
        structuredWrite[index] = 0xb2000000 + index;
    }
    surface[uint2(1, 0)] = uint4(0xc1000011, 0xc2000022, 0xc3000033, 0xc4000044);
    output[word] = 0xc001c0de;
}

// Preserve the low and high halves of Double without relying on a numeric conversion.
uint64_t observeDouble(double value)
{
    uint low, high;
    asuint(value, low, high);
    return uint64_t(low) | (uint64_t(high) << 32);
}

// This separate entry covers all twelve existing scalar kinds and their vectors. Each UInt
// tail follows the CUDA storage extent, which includes Half3 padding but not a fourth lane.
[shader("compute")]
[numthreads(1, 1, 1)]
void numericEntryMain(
    uniform int8_t i8_1, uniform uint tail_i8_1,
    uniform vector<int8_t, 2> i8_2, uniform uint tail_i8_2,
    uniform vector<int8_t, 3> i8_3, uniform uint tail_i8_3,
    uniform vector<int8_t, 4> i8_4, uniform uint tail_i8_4,
    uniform uint8_t u8_1, uniform uint tail_u8_1,
    uniform vector<uint8_t, 2> u8_2, uniform uint tail_u8_2,
    uniform vector<uint8_t, 3> u8_3, uniform uint tail_u8_3,
    uniform vector<uint8_t, 4> u8_4, uniform uint tail_u8_4,
    uniform int16_t i16_1, uniform uint tail_i16_1,
    uniform vector<int16_t, 2> i16_2, uniform uint tail_i16_2,
    uniform vector<int16_t, 3> i16_3, uniform uint tail_i16_3,
    uniform vector<int16_t, 4> i16_4, uniform uint tail_i16_4,
    uniform uint16_t u16_1, uniform uint tail_u16_1,
    uniform vector<uint16_t, 2> u16_2, uniform uint tail_u16_2,
    uniform vector<uint16_t, 3> u16_3, uniform uint tail_u16_3,
    uniform vector<uint16_t, 4> u16_4, uniform uint tail_u16_4,
    uniform int i32_1, uniform uint tail_i32_1,
    uniform vector<int, 2> i32_2, uniform uint tail_i32_2,
    uniform vector<int, 3> i32_3, uniform uint tail_i32_3,
    uniform vector<int, 4> i32_4, uniform uint tail_i32_4,
    uniform uint u32_1, uniform uint tail_u32_1,
    uniform vector<uint, 2> u32_2, uniform uint tail_u32_2,
    uniform vector<uint, 3> u32_3, uniform uint tail_u32_3,
    uniform vector<uint, 4> u32_4, uniform uint tail_u32_4,
    uniform int64_t i64_1, uniform uint tail_i64_1,
    uniform vector<int64_t, 2> i64_2, uniform uint tail_i64_2,
    uniform vector<int64_t, 3> i64_3, uniform uint tail_i64_3,
    uniform vector<int64_t, 4> i64_4, uniform uint tail_i64_4,
    uniform uint64_t u64_1, uniform uint tail_u64_1,
    uniform vector<uint64_t, 2> u64_2, uniform uint tail_u64_2,
    uniform vector<uint64_t, 3> u64_3, uniform uint tail_u64_3,
    uniform vector<uint64_t, 4> u64_4, uniform uint tail_u64_4,
    uniform half h_1, uniform uint tail_h_1,
    uniform vector<half, 2> h_2, uniform uint tail_h_2,
    uniform vector<half, 3> h_3, uniform uint tail_h_3,
    uniform vector<half, 4> h_4, uniform uint tail_h_4,
    uniform float f_1, uniform uint tail_f_1,
    uniform vector<float, 2> f_2, uniform uint tail_f_2,
    uniform vector<float, 3> f_3, uniform uint tail_f_3,
    uniform vector<float, 4> f_4, uniform uint tail_f_4,
    uniform double d_1, uniform uint tail_d_1,
    uniform vector<double, 2> d_2, uniform uint tail_d_2,
    uniform vector<double, 3> d_3, uniform uint tail_d_3,
    uniform vector<double, 4> d_4, uniform uint tail_d_4,
    uniform bool b_1, uniform uint tail_b_1,
    uniform vector<bool, 2> b_2, uniform uint tail_b_2,
    uniform vector<bool, 3> b_3, uniform uint tail_b_3,
    uniform vector<bool, 4> b_4, uniform uint tail_b_4,
    RWStructuredBuffer<uint64_t> output)
{
    uint word = 1;
    output[word++] = uint64_t(i8_1);
    output[word++] = uint64_t(tail_i8_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(i8_2[lane]);
    output[word++] = uint64_t(tail_i8_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(i8_3[lane]);
    output[word++] = uint64_t(tail_i8_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(i8_4[lane]);
    output[word++] = uint64_t(tail_i8_4);
    output[word++] = uint64_t(u8_1);
    output[word++] = uint64_t(tail_u8_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(u8_2[lane]);
    output[word++] = uint64_t(tail_u8_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(u8_3[lane]);
    output[word++] = uint64_t(tail_u8_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(u8_4[lane]);
    output[word++] = uint64_t(tail_u8_4);
    output[word++] = uint64_t(i16_1);
    output[word++] = uint64_t(tail_i16_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(i16_2[lane]);
    output[word++] = uint64_t(tail_i16_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(i16_3[lane]);
    output[word++] = uint64_t(tail_i16_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(i16_4[lane]);
    output[word++] = uint64_t(tail_i16_4);
    output[word++] = uint64_t(u16_1);
    output[word++] = uint64_t(tail_u16_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(u16_2[lane]);
    output[word++] = uint64_t(tail_u16_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(u16_3[lane]);
    output[word++] = uint64_t(tail_u16_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(u16_4[lane]);
    output[word++] = uint64_t(tail_u16_4);
    output[word++] = uint64_t(i32_1);
    output[word++] = uint64_t(tail_i32_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(i32_2[lane]);
    output[word++] = uint64_t(tail_i32_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(i32_3[lane]);
    output[word++] = uint64_t(tail_i32_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(i32_4[lane]);
    output[word++] = uint64_t(tail_i32_4);
    output[word++] = uint64_t(u32_1);
    output[word++] = uint64_t(tail_u32_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(u32_2[lane]);
    output[word++] = uint64_t(tail_u32_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(u32_3[lane]);
    output[word++] = uint64_t(tail_u32_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(u32_4[lane]);
    output[word++] = uint64_t(tail_u32_4);
    output[word++] = uint64_t(i64_1);
    output[word++] = uint64_t(tail_i64_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(i64_2[lane]);
    output[word++] = uint64_t(tail_i64_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(i64_3[lane]);
    output[word++] = uint64_t(tail_i64_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(i64_4[lane]);
    output[word++] = uint64_t(tail_i64_4);
    output[word++] = uint64_t(u64_1);
    output[word++] = uint64_t(tail_u64_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(u64_2[lane]);
    output[word++] = uint64_t(tail_u64_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(u64_3[lane]);
    output[word++] = uint64_t(tail_u64_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(u64_4[lane]);
    output[word++] = uint64_t(tail_u64_4);
    output[word++] = uint64_t(asuint16(h_1));
    output[word++] = uint64_t(tail_h_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(asuint16(h_2[lane]));
    output[word++] = uint64_t(tail_h_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(asuint16(h_3[lane]));
    output[word++] = uint64_t(tail_h_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(asuint16(h_4[lane]));
    output[word++] = uint64_t(tail_h_4);
    output[word++] = uint64_t(asuint(f_1));
    output[word++] = uint64_t(tail_f_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(asuint(f_2[lane]));
    output[word++] = uint64_t(tail_f_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(asuint(f_3[lane]));
    output[word++] = uint64_t(tail_f_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(asuint(f_4[lane]));
    output[word++] = uint64_t(tail_f_4);
    output[word++] = observeDouble(d_1);
    output[word++] = uint64_t(tail_d_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = observeDouble(d_2[lane]);
    output[word++] = uint64_t(tail_d_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = observeDouble(d_3[lane]);
    output[word++] = uint64_t(tail_d_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = observeDouble(d_4[lane]);
    output[word++] = uint64_t(tail_d_4);
    output[word++] = uint64_t(b_1);
    output[word++] = uint64_t(tail_b_1);
    for (uint lane = 0; lane < 2; ++lane) output[word++] = uint64_t(b_2[lane]);
    output[word++] = uint64_t(tail_b_2);
    for (uint lane = 0; lane < 3; ++lane) output[word++] = uint64_t(b_3[lane]);
    output[word++] = uint64_t(tail_b_3);
    for (uint lane = 0; lane < 4; ++lane) output[word++] = uint64_t(b_4[lane]);
    output[word++] = uint64_t(tail_b_4);
    output[word] = uint64_t(0xc001c0de);
}

// Query all writable geometries independently of texel loads/stores. Array sizes must not
// be confused with spatial height/depth; the half surface also exercises a different format.
[shader("compute")]
[numthreads(1, 1, 1)]
void surfaceDimensionsMain(
    RWTexture1D<float> oneD,
    RWTexture2D<float> twoD,
    RWTexture3D<float> threeD,
    RWTexture1DArray<float> oneDArray,
    RWTexture2DArray<float> twoDArray,
    [format("r16f")] RWTexture2D<half> half2D,
    RWStructuredBuffer<uint> output)
{
    uint width, height, depth, layers;
    oneD.GetDimensions(width);
    output[1] = width;
    twoD.GetDimensions(width, height);
    output[2] = width;
    output[3] = height;
    threeD.GetDimensions(width, height, depth);
    output[4] = width;
    output[5] = height;
    output[6] = depth;
    oneDArray.GetDimensions(width, layers);
    output[7] = width;
    output[8] = layers;
    twoDArray.GetDimensions(width, height, layers);
    output[9] = width;
    output[10] = height;
    output[11] = layers;
    half2D.GetDimensions(width, height);
    output[12] = width;
    output[13] = height;
    output[14] = 0xc001c0de;
}

#include "testing.h"
#include "../examples/base/math.h"

#include <cstring>
#include <limits>
#include <type_traits>

using namespace rhi;
using namespace rhi::math;
using namespace rhi::testing;

namespace {

template<typename T, size_t FloatCount>
constexpr bool packed()
{
    return std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> && sizeof(T) == FloatCount * sizeof(float) &&
           alignof(T) == alignof(float);
}

static_assert(packed<float2, 2>() && packed<float3, 3>() && packed<float4, 4>());
static_assert(packed<float2x2, 4>() && packed<float2x3, 6>() && packed<float2x4, 8>());
static_assert(packed<float3x2, 6>() && packed<float3x3, 9>() && packed<float3x4, 12>());
static_assert(packed<float4x2, 8>() && packed<float4x3, 12>() && packed<float4x4, 16>());
static_assert(offsetof(float3, y) == sizeof(float) && offsetof(float3, z) == 2 * sizeof(float));

template<size_t N>
void checkVector(const Vector<N>& actual, const Vector<N>& expected)
{
    for (size_t i = 0; i < N; ++i)
        CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(1e-5).scale(1));
}

template<size_t M, size_t N>
void checkMatrix(const rhi::math::Matrix<M, N>& actual, const rhi::math::Matrix<M, N>& expected)
{
    for (size_t i = 0; i < M; ++i)
        checkVector(actual[i], expected[i]);
}

} // namespace

TEST_CASE("example-math-vectors")
{
    constexpr float3 zero;
    static_assert(zero.x == 0 && zero.y == 0 && zero.z == 0);
    constexpr float3 splat(2);
    static_assert(dot(splat, splat) == 12);

    float3 v(1, 2, 3);
    v[1] = 4;
    CHECK(v.y == 4);
    checkVector(v + float3(2), float3(3, 6, 5));
    checkVector(v - float3(2), float3(-1, 2, 1));
    checkVector(v + 2.f, float3(3, 6, 5));
    checkVector(2.f + v, float3(3, 6, 5));
    checkVector(v - 2.f, float3(-1, 2, 1));
    checkVector(2.f - v, float3(1, -2, -1));
    checkVector(12.f / v, float3(12, 3, 4));
    checkVector(-v, float3(-1, -4, -3));
    checkVector(v * float3(2, 3, 4), float3(2, 12, 12));
    checkVector(v / float3(1, 2, 3), float3(1, 2, 1));
    v += float3(1);
    v -= float3(1);
    v *= float3(2);
    v /= float3(2);
    v *= 2;
    v /= 2;
    v += 2;
    v -= 2;
    checkVector(2.f * v, float3(2, 8, 6));
    CHECK(length(float2(3, 4)) == doctest::Approx(5));
    checkVector(normalize(float3(0, 3, 4)), float3(0, 0.6f, 0.8f));
    checkVector(normalize(float3()), float3());
    checkVector(cross(float3(1, 0, 0), float3(0, 1, 0)), float3(0, 0, 1));
    checkVector(float4(v, 1), float4(1, 4, 3, 1));
}

TEST_CASE("example-math-row-major")
{
    constexpr float2x3 a = {{{1, 2, 3}, {4, 5, 6}}};
    constexpr float3x2 b = {{{7, 8}, {9, 10}, {11, 12}}};
    constexpr float2x2 product = mul(a, b);
    static_assert(product[0][0] == 58 && product[0][1] == 64);
    static_assert(product[1][0] == 139 && product[1][1] == 154);
    checkVector(mul(a, float3(1, 2, 3)), float2(14, 32));
    checkVector(mul(float2(2, 3), a), float3(14, 19, 24));
    checkMatrix(transpose(a), float3x2{{{1, 4}, {2, 5}, {3, 6}}});
    checkMatrix(transpose(transpose(a)), a);
    checkMatrix(mul(a, identityMatrix<3>()), a);

    float storage[6];
    std::memcpy(storage, &a, sizeof(a));
    for (size_t i = 0; i < 6; ++i)
        CHECK(storage[i] == float(i + 1));
    float3x4 affine = {{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}}};
    AccelerationStructureInstanceDescGeneric instance = {};
    std::memcpy(instance.transform, &affine, sizeof(affine));
    CHECK(instance.transform[0][3] == 4);
    CHECK(instance.transform[1][0] == 5);
    CHECK(instance.transform[2][3] == 12);
}

TEST_CASE("example-math-transforms")
{
    const float quarterTurn = 1.5707963267948966f;
    checkVector(mul(rotationX(quarterTurn), float4(0, 1, 0, 0)), float4(0, 0, 1, 0));
    checkVector(mul(rotationY(quarterTurn), float4(0, 0, 1, 0)), float4(1, 0, 0, 0));
    checkVector(mul(rotationZ(quarterTurn), float4(1, 0, 0, 0)), float4(0, 1, 0, 0));

    const float4x4 rotation = mul(mul(rotationZ(quarterTurn), rotationY(quarterTurn)), rotationX(quarterTurn));
    checkVector(mul(rotation, float4(1, 2, 3, 0)), float4(3, 2, -1, 0));
    const float4x4 transform =
        mul(translationMatrix(float3(10, 20, 30)), mul(rotationZ(quarterTurn), scalingMatrix(float3(2, 3, 4))));
    checkVector(mul(transform, float4(1, 2, 3, 1)), float4(4, 22, 42, 1));
    checkVector(mul(transform, float4(1, 2, 3, 0)), float4(-6, 2, 12, 0));

    float4x4 inverseTransform;
    REQUIRE(tryInverse(transform, inverseTransform));
    checkMatrix(mul(transform, inverseTransform), identityMatrix<4>());
    checkMatrix(mul(inverseTransform, transform), identityMatrix<4>());
    checkVector(mul(inverseTransform, mul(transform, float4(1, 2, 3, 1))), float4(1, 2, 3, 1));
    // Inverse-transpose normals stay perpendicular to tangents under nonuniform scale.
    const float4 tangent = mul(transform, float4(1, 1, 0, 0));
    const float4 normal = mul(transpose(inverseTransform), float4(1, -1, 0, 0));
    CHECK(
        dot(float3(tangent.x, tangent.y, tangent.z), float3(normal.x, normal.y, normal.z)) ==
        doctest::Approx(0).epsilon(1e-5).scale(1)
    );
}

TEST_CASE("example-math-inverse")
{
    const float2x2 swap = {{{0, 2}, {4, 0}}};
    float2x2 inverse2;
    REQUIRE(tryInverse(swap, inverse2));
    checkMatrix(inverse2, float2x2{{{0, 0.25f}, {0.5f, 0}}});
    const float3x3 matrix3 = {{{1, 2, 3}, {0, 1, 4}, {5, 6, 0}}};
    float3x3 inverse3;
    REQUIRE(tryInverse(matrix3, inverse3));
    checkMatrix(inverse3, float3x3{{{-24, 18, 5}, {20, -15, -4}, {-5, 4, 1}}});
    // Exercise a non-affine matrix, including a pivot swap and in-place inversion.
    float4x4 matrix4 = {{{0, 2, 0, 0}, {4, 0, 0, 0}, {0, 0, 2, 1}, {0, 0, 1, 1}}};
    REQUIRE(tryInverse(matrix4, matrix4));
    checkMatrix(matrix4, float4x4{{{0, 0.25f, 0, 0}, {0.5f, 0, 0, 0}, {0, 0, 1, -1}, {0, 0, -1, 2}}});

    float4x4 out = identityMatrix<4>();
    CHECK_FALSE(tryInverse(scalingMatrix(float3(1, 0, 1)), out));
    checkMatrix(out, identityMatrix<4>());
    CHECK_FALSE(tryInverse(scalingMatrix(float3(std::numeric_limits<float>::denorm_min(), 1, 1)), out));
    checkMatrix(out, identityMatrix<4>());
    float2x2 singular = {{{1, 2}, {2, 4}}};
    CHECK_FALSE(tryInverse(singular, inverse2));
    for (float value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        float4x4 invalid = identityMatrix<4>();
        invalid[0][0] = value;
        CHECK_FALSE(tryInverse(invalid, out));
        checkMatrix(out, identityMatrix<4>());
    }
}

GPU_TEST_CASE("example-math-shader-layout", ALL)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-example-math", "computeMain", program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);

    // Two elements catch both row order and array-stride mismatches. Four-wide rows
    // are directly uploadable across backends; narrower rows can require shader padding.
    const float4x4 transforms[] = {
        {{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {0, 0, 0, 1}}},
        mul(translationMatrix(float3(10, 20, 30)), scalingMatrix(float3(2, 3, 4))),
    };
    const float3x4 affineTransforms[] = {
        {{transforms[0][0], transforms[0][1], transforms[0][2]}},
        {{transforms[1][0], transforms[1][1], transforms[1][2]}},
    };
    BufferDesc desc = {};
    desc.usage = BufferUsage::ShaderResource;
    desc.size = sizeof(transforms);
    desc.elementSize = sizeof(float4x4);
    auto matrices = device->createBuffer(desc, transforms);
    REQUIRE(matrices);
    desc.size = sizeof(affineTransforms);
    desc.elementSize = sizeof(float3x4);
    auto affineMatrices = device->createBuffer(desc, affineTransforms);
    REQUIRE(affineMatrices);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    desc.size = 4 * sizeof(float4);
    desc.elementSize = sizeof(float4);
    auto output = device->createBuffer(desc);
    REQUIRE(output);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["matrices"].setBinding(matrices));
    REQUIRE_CALL(cursor["affineMatrices"].setBinding(affineMatrices));
    REQUIRE_CALL(cursor["output"].setBinding(output));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    queue->submit(encoder->finish());
    queue->waitOnHost();
    compareComputeResult(
        device,
        output,
        std::array<float, 16>{13, 33, 53, 1, 13, 33, 53, 1, 14, 17, 42, 1, 14, 17, 42, 1}
    );
}

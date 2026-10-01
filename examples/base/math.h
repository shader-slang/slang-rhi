#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

// Small scalar math types for examples. Matrices use M rows and N columns, stored
// contiguously in row-major order. Transforms act on column vectors: mul(M, v).
// Translation occupies the last column, and mul(T, mul(R, S)) applies S, then R, then T.
// All types are zero-initialized and tightly packed (float3 is 12 bytes). Shader
// constant-buffer and matrix padding still depends on the reflected shader layout;
// these types do not insert GPU-specific padding.
namespace rhi::math {

template<size_t N>
struct Vector;

template<>
struct Vector<2>
{
    float x = 0, y = 0;

    constexpr Vector() = default;
    constexpr explicit Vector(float value)
        : x(value)
        , y(value)
    {
    }
    constexpr Vector(float x, float y)
        : x(x)
        , y(y)
    {
    }

    constexpr float& operator[](size_t i)
    {
        assert(i < 2);
        return i == 0 ? x : y;
    }
    constexpr const float& operator[](size_t i) const
    {
        assert(i < 2);
        return i == 0 ? x : y;
    }
};

template<>
struct Vector<3>
{
    float x = 0, y = 0, z = 0;

    constexpr Vector() = default;
    constexpr explicit Vector(float value)
        : x(value)
        , y(value)
        , z(value)
    {
    }
    constexpr Vector(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z)
    {
    }

    constexpr float& operator[](size_t i)
    {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
    constexpr const float& operator[](size_t i) const
    {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
};

template<>
struct Vector<4>
{
    float x = 0, y = 0, z = 0, w = 0;

    constexpr Vector() = default;
    constexpr explicit Vector(float value)
        : x(value)
        , y(value)
        , z(value)
        , w(value)
    {
    }
    constexpr Vector(float x, float y, float z, float w)
        : x(x)
        , y(y)
        , z(z)
        , w(w)
    {
    }
    constexpr Vector(const Vector<3>& xyz, float w)
        : x(xyz.x)
        , y(xyz.y)
        , z(xyz.z)
        , w(w)
    {
    }

    constexpr float& operator[](size_t i)
    {
        assert(i < 4);
        return i == 0 ? x : i == 1 ? y : i == 2 ? z : w;
    }
    constexpr const float& operator[](size_t i) const
    {
        assert(i < 4);
        return i == 0 ? x : i == 1 ? y : i == 2 ? z : w;
    }
};

using float2 = Vector<2>;
using float3 = Vector<3>;
using float4 = Vector<4>;

// Vector arithmetic is component-wise.
template<size_t N>
constexpr Vector<N> operator+(Vector<N> a, const Vector<N>& b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] += b[i];
    return a;
}

template<size_t N>
constexpr Vector<N> operator-(Vector<N> a, const Vector<N>& b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] -= b[i];
    return a;
}

template<size_t N>
constexpr Vector<N> operator*(Vector<N> a, const Vector<N>& b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] *= b[i];
    return a;
}

template<size_t N>
constexpr Vector<N> operator/(Vector<N> a, const Vector<N>& b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] /= b[i];
    return a;
}

template<size_t N>
constexpr Vector<N> operator*(Vector<N> a, float b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] *= b;
    return a;
}

template<size_t N>
constexpr Vector<N> operator/(Vector<N> a, float b)
{
    for (size_t i = 0; i < N; ++i)
        a[i] /= b;
    return a;
}

template<size_t N>
constexpr Vector<N> operator*(float a, const Vector<N>& b)
{
    return b * a;
}

template<size_t N>
constexpr Vector<N> operator-(Vector<N> v)
{
    for (size_t i = 0; i < N; ++i)
        v[i] = -v[i];
    return v;
}

template<size_t N>
constexpr Vector<N>& operator+=(Vector<N>& a, const Vector<N>& b)
{
    return a = a + b;
}

template<size_t N>
constexpr Vector<N>& operator-=(Vector<N>& a, const Vector<N>& b)
{
    return a = a - b;
}

template<size_t N>
constexpr Vector<N>& operator*=(Vector<N>& a, const Vector<N>& b)
{
    return a = a * b;
}

template<size_t N>
constexpr Vector<N>& operator/=(Vector<N>& a, const Vector<N>& b)
{
    return a = a / b;
}

template<size_t N>
constexpr Vector<N>& operator*=(Vector<N>& a, float b)
{
    return a = a * b;
}

template<size_t N>
constexpr Vector<N>& operator/=(Vector<N>& a, float b)
{
    return a = a / b;
}

template<size_t N>
constexpr Vector<N> operator+(const Vector<N>& a, float b)
{
    return a + Vector<N>(b);
}

template<size_t N>
constexpr Vector<N> operator-(const Vector<N>& a, float b)
{
    return a - Vector<N>(b);
}

template<size_t N>
constexpr Vector<N> operator+(float a, const Vector<N>& b)
{
    return Vector<N>(a) + b;
}

template<size_t N>
constexpr Vector<N> operator-(float a, const Vector<N>& b)
{
    return Vector<N>(a) - b;
}

template<size_t N>
constexpr Vector<N> operator/(float a, const Vector<N>& b)
{
    return Vector<N>(a) / b;
}

template<size_t N>
constexpr Vector<N>& operator+=(Vector<N>& a, float b)
{
    return a = a + b;
}

template<size_t N>
constexpr Vector<N>& operator-=(Vector<N>& a, float b)
{
    return a = a - b;
}

template<size_t N>
constexpr bool operator==(const Vector<N>& a, const Vector<N>& b)
{
    for (size_t i = 0; i < N; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

template<size_t N>
constexpr float dot(const Vector<N>& a, const Vector<N>& b)
{
    float result = 0;
    for (size_t i = 0; i < N; ++i)
        result += a[i] * b[i];
    return result;
}

constexpr float3 cross(const float3& a, const float3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

template<size_t N>
float length(const Vector<N>& v)
{
    return std::sqrt(dot(v, v));
}

// Normalizing the zero vector returns the zero vector.
template<size_t N>
Vector<N> normalize(const Vector<N>& v)
{
    float len = length(v);
    return len > 0 ? v / len : Vector<N>{};
}

template<size_t M, size_t N>
struct Matrix
{
    static_assert(M >= 2 && M <= 4 && N >= 2 && N <= 4);
    Vector<N> rows[M] = {};

    constexpr Vector<N>& operator[](size_t row)
    {
        assert(row < M);
        return rows[row];
    }
    constexpr const Vector<N>& operator[](size_t row) const
    {
        assert(row < M);
        return rows[row];
    }
};

using float2x2 = Matrix<2, 2>;
using float2x3 = Matrix<2, 3>;
using float2x4 = Matrix<2, 4>;
using float3x2 = Matrix<3, 2>;
using float3x3 = Matrix<3, 3>;
using float3x4 = Matrix<3, 4>;
using float4x2 = Matrix<4, 2>;
using float4x3 = Matrix<4, 3>;
using float4x4 = Matrix<4, 4>;

template<size_t N>
constexpr Matrix<N, N> identityMatrix()
{
    Matrix<N, N> result;
    for (size_t i = 0; i < N; ++i)
        result[i][i] = 1;
    return result;
}

template<size_t M, size_t N>
constexpr Matrix<N, M> transpose(const Matrix<M, N>& m)
{
    Matrix<N, M> result;
    for (size_t i = 0; i < M; ++i)
        for (size_t j = 0; j < N; ++j)
            result[j][i] = m[i][j];
    return result;
}

template<size_t M, size_t N>
constexpr Vector<M> mul(const Matrix<M, N>& m, const Vector<N>& v)
{
    Vector<M> result;
    for (size_t i = 0; i < M; ++i)
        result[i] = dot(m[i], v);
    return result;
}

template<size_t M, size_t N>
constexpr Vector<N> mul(const Vector<M>& v, const Matrix<M, N>& m)
{
    Vector<N> result;
    for (size_t i = 0; i < M; ++i)
        result += v[i] * m[i];
    return result;
}

template<size_t M, size_t K, size_t N>
constexpr Matrix<M, N> mul(const Matrix<M, K>& a, const Matrix<K, N>& b)
{
    Matrix<M, N> result;
    for (size_t i = 0; i < M; ++i)
        result[i] = mul(a[i], b);
    return result;
}

// Gauss-Jordan elimination with partial pivoting and double intermediates.
// Returns false for a zero pivot, non-finite input, or a result outside the finite
// float range, leaving out unchanged.
// This does not estimate conditioning; callers should avoid nearly singular matrices.
template<size_t N>
bool tryInverse(const Matrix<N, N>& m, Matrix<N, N>& out)
{
    double augmented[N][2 * N] = {};
    for (size_t i = 0; i < N; ++i)
    {
        for (size_t j = 0; j < N; ++j)
        {
            if (!std::isfinite(m[i][j]))
                return false;
            augmented[i][j] = m[i][j];
        }
        augmented[i][N + i] = 1;
    }

    for (size_t column = 0; column < N; ++column)
    {
        size_t pivot = column;
        for (size_t row = column + 1; row < N; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
                pivot = row;
        if (augmented[pivot][column] == 0)
            return false;

        for (size_t j = 0; j < 2 * N; ++j)
        {
            double temp = augmented[column][j];
            augmented[column][j] = augmented[pivot][j];
            augmented[pivot][j] = temp;
        }
        double divisor = augmented[column][column];
        for (size_t j = 0; j < 2 * N; ++j)
            augmented[column][j] /= divisor;
        for (size_t row = 0; row < N; ++row)
        {
            if (row == column)
                continue;
            double factor = augmented[row][column];
            for (size_t j = 0; j < 2 * N; ++j)
                augmented[row][j] -= factor * augmented[column][j];
        }
    }

    Matrix<N, N> result;
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < N; ++j)
        {
            double value = augmented[i][N + j];
            if (!std::isfinite(value) || std::abs(value) > (std::numeric_limits<float>::max)())
                return false;
            result[i][j] = float(value);
        }
    out = result;
    return true;
}

constexpr float4x4 translationMatrix(const float3& translation)
{
    float4x4 result = identityMatrix<4>();
    for (size_t i = 0; i < 3; ++i)
        result[i][3] = translation[i];
    return result;
}

constexpr float4x4 scalingMatrix(const float3& scale)
{
    float4x4 result = identityMatrix<4>();
    for (size_t i = 0; i < 3; ++i)
        result[i][i] = scale[i];
    return result;
}

// Right-handed rotations; angles are in radians.
inline float4x4 rotationX(float angle)
{
    float4x4 result = identityMatrix<4>();
    float c = std::cos(angle), s = std::sin(angle);
    result[1][1] = c;
    result[1][2] = -s;
    result[2][1] = s;
    result[2][2] = c;
    return result;
}

inline float4x4 rotationY(float angle)
{
    float4x4 result = identityMatrix<4>();
    float c = std::cos(angle), s = std::sin(angle);
    result[0][0] = c;
    result[0][2] = s;
    result[2][0] = -s;
    result[2][2] = c;
    return result;
}

inline float4x4 rotationZ(float angle)
{
    float4x4 result = identityMatrix<4>();
    float c = std::cos(angle), s = std::sin(angle);
    result[0][0] = c;
    result[0][1] = -s;
    result[1][0] = s;
    result[1][1] = c;
    return result;
}

} // namespace rhi::math

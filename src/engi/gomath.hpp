#pragma once

#include <limits>
#include <type_traits>
#include <concepts>
#include <cstdint>
#include <cmath>

namespace go
{

constexpr size_t x = 0;
constexpr size_t y = 1;
constexpr size_t z = 2;
constexpr size_t w = 3;

template<typename T>
concept Field = std::is_arithmetic<T>::value;

template<typename T>
concept RealField = std::is_floating_point<T>::value;

template <typename T>
concept Vector = Field<typename T::field> && requires (T v)
{ 
    {T::rank()} -> std::same_as<size_t>;
    {v[1]} -> std::convertible_to<typename T::field>;
    {v + v} -> std::same_as<T>;
    {v - v} -> std::same_as<T>;
    {  - v} -> std::same_as<T>;
};

template <typename T>
concept RealVector = RealField<typename T::field> 
&& requires (T v) 
{
    {T::rank()} -> std::same_as<size_t>;
    {v[1]} -> std::convertible_to<typename T::field>;
    {v + v} -> std::same_as<T>;
    {v - v} -> std::same_as<T>;
    {  - v} -> std::same_as<T>;
};

template<Field T, size_t c>
struct vec
{
    using type = vec<T, c>;
    using field = T;
    constexpr static auto rank() -> size_t  { return c; }

    T data[c];

    inline constexpr auto operator[](size_t i) noexcept -> T& { return data[i]; }
    inline constexpr auto operator[](size_t i) const noexcept -> const T& { return data[i]; }

    inline auto operator+=(const type& v) noexcept -> void
    {
        data[0] += v.data[0];
        data[1] += v.data[1];
        if constexpr (c > 2) data[2] += v.data[2];
        if constexpr (c > 3) data[3] += v.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            data[i] += v.data[i];
    }
    inline auto operator-=(const type& v) noexcept -> void
    {
        data[0] -= v.data[0];
        data[1] -= v.data[1];
        if constexpr (c > 2) data[2] -= v.data[2];
        if constexpr (c > 3) data[3] -= v.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            data[i] -= v.data[i];
    }

    inline constexpr auto operator+(const type& v) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] + v.data[0];
        res.data[1] = data[1] + v.data[1];
        if constexpr (c > 2) res.data[2] = data[2] + v.data[2];
        if constexpr (c > 3) res.data[3] = data[3] + v.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] + v.data[i];
        return res;
    }

    /*inline constexpr auto operator+(const T& v) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] + v;
        res.data[1] = data[1] + v;
        if constexpr (c > 2) res.data[2] = data[2] + v;
        if constexpr (c > 3) res.data[3] = data[3] + v;
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] + v;
        return res;
    }*/

    inline constexpr auto operator*(const type& v) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] * v.data[0];
        res.data[1] = data[1] * v.data[1];
        if constexpr (c > 2) res.data[2] = data[2] * v.data[2];
        if constexpr (c > 3) res.data[3] = data[3] * v.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] * v.data[i];
        return res;
    }
    inline constexpr auto operator*(const T& v) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] * v;
        res.data[1] = data[1] * v;
        if constexpr (c > 2) res.data[2] = data[2] * v;
        if constexpr (c > 3) res.data[3] = data[3] * v;
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] * v;
        return res;
    }
    inline constexpr auto operator/(const T& v) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] / v;
        res.data[1] = data[1] / v;
        if constexpr (c > 2) res.data[2] = data[2] / v;
        if constexpr (c > 3) res.data[3] = data[3] / v;
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] / v;
        return res;
    }
    inline constexpr auto operator/(const type& rhs) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] / rhs.data[0];
        res.data[1] = data[1] / rhs.data[1];
        if constexpr (c > 2) res.data[2] = data[2] / rhs.data[2];
        if constexpr (c > 3) res.data[3] = data[3] / rhs.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] / rhs.data[i];
        return res;
    }
    inline constexpr auto operator-(const type& rhs) const noexcept -> type
    {
        type res;
        res.data[0] = data[0] - rhs.data[0];
        res.data[1] = data[1] - rhs.data[1];
        if constexpr (c > 2) res.data[2] = data[2] - rhs.data[2];
        if constexpr (c > 3) res.data[3] = data[3] - rhs.data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = data[i] - rhs.data[i];
        return res;
    }
    inline constexpr auto operator-() const noexcept -> type 
    {
        type res;
        res.data[0] = - data[0];
        res.data[1] = - data[1];
        if constexpr (c > 2) res.data[2] = - data[2];
        if constexpr (c > 3) res.data[3] = - data[3];
        if constexpr (c > 4) for (size_t i = 4; i < c; i++) 
            res.data[i] = - data[i];
        return res;
    }

    template<size_t OUT>
    inline auto shrink() const noexcept -> vec<field, OUT>
        requires (rank() > OUT)
    {    
        if constexpr (OUT == 2)
            return { data[0], data[1]};
        else if constexpr (OUT == 3)
            return { data[0], data[1], data[2]};
        else if constexpr (OUT == 4)
            return { data[0], data[1], data[2], data[3]};
        else
        {
            vec<field, OUT> out;
            for (size_t i = 0; i < OUT; i++)
                out[i] = data[i];
            return out;
        }
    }

    template<Field To>
    inline auto cast() const noexcept -> vec<To, c>
    {
        if constexpr(c == 2)
            return {To(data[0]), To(data[1])};
        else if constexpr (c == 3)
            return {To(data[0]), To(data[1]), To(data[2])};
        else if constexpr (c == 4)
            return {To(data[0]), To(data[1]), To(data[2]), To(data[3])};
        else
        {
            vec<To, c> output;
            for (size_t i = 0; i < c; i++)
                output[i] = To(data[i]);
            return output;
        }
    }

    static inline auto zero() noexcept -> type
    {
        return {0};
    }

    static inline auto from(T f) noexcept -> type
    {
        if constexpr(c == 2)
            return {f, f};
        else if constexpr (c == 3)
            return {f, f, f};
        else if constexpr (c == 4)
            return {f, f, f, f};
        else
        {
            type output;
            for (size_t i = 0; i < c; i++)
                output[i] = f;
            return output;
        }
    }
};

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using f32 = float;
using f64 = double;

using vf2 = vec<f32, 2>; using vf3 = vec<f32, 3>; using vf4 = vec<f32, 4>;
using vd2 = vec<f64, 2>; using vd3 = vec<f64, 3>; using vd4 = vec<f64, 4>;
using vi2 = vec<i32, 2>; using vi3 = vec<i32, 3>; using vi4 = vec<i32, 4>;
using vu2 = vec<u32, 2>; using vu3 = vec<u32, 3>; using vu4 = vec<u32, 4>;

template<Field T>
using v2 = vec<T, 2>;
template<Field T>
using v3 = vec<T, 3>;
template<Field T>
using v4 = vec<T, 4>;

template<Vector T>
inline constexpr auto operator*(const typename T::field& s, const T& v) noexcept -> T
{
    T res;
    res.data[0] = s * v.data[0];
    res.data[1] = s * v.data[1];
    if constexpr (T::rank() > 2) res.data[2] = s * v.data[2];
    if constexpr (T::rank() > 3) res.data[3] = s * v.data[3];
    if constexpr (T::rank() > 4) for (size_t i = 4; i < v.rank(); i++) 
        res.data[i] = s * v.data[i];
    return res;
}

template<RealVector T> 
inline auto equal(const T& v1, const T& v2, typename T::field delta) noexcept -> bool
{
    for (size_t i = 0; i < T::rank(); i++)
            if (abs(v1[i] - v2[i]) > delta)
                return false;
    return true;
}

template<Vector T>
inline auto equal(const T& v1, const T& v2) noexcept -> bool
{
    for (size_t i = 0; i < T::rank(); i++)
            if (v1[i] != v2[i])
                return false;
    return true;
}

template<Vector T>
inline auto min(const T& v1, const T& v2) noexcept -> T
{
    if constexpr (T::rank() == 2)
        return 
        { 
            v1[0] < v2[0] ? v1[0] : v2[0], 
            v1[1] < v2[1] ? v1[1] : v2[1] 
        };
    else if constexpr (T::rank() == 3)
        return 
        { 
            v1[0] < v2[0] ? v1[0] : v2[0], 
            v1[1] < v2[1] ? v1[1] : v2[1],
            v1[2] < v2[2] ? v1[2] : v2[2]
        };
    else if constexpr (T::rank() == 4)
        return 
        { 
            v1[0] < v2[0] ? v1[0] : v2[0], 
            v1[1] < v2[1] ? v1[1] : v2[1],
            v1[2] < v2[2] ? v1[2] : v2[2],
            v1[3] < v2[3] ? v1[3] : v2[3]
        };
    else
    {
        T output;
        for (size_t i = 0; i < T::rank(); i++)
            output[i] = v1[i] < v2[i] ? v1[i] : v2[i];
        return output;
    }
}

template<Vector T>
inline auto max(const T& v1, const T& v2) noexcept -> T
{
    if constexpr (T::rank() == 2)
        return 
        { 
            v1[0] > v2[0] ? v1[0] : v2[0], 
            v1[1] > v2[1] ? v1[1] : v2[1] 
        };
    else if constexpr (T::rank() == 3)
        return 
        { 
            v1[0] > v2[0] ? v1[0] : v2[0], 
            v1[1] > v2[1] ? v1[1] : v2[1],
            v1[2] > v2[2] ? v1[2] : v2[2]
        };
    else if constexpr (T::rank() == 4)
        return 
        { 
            v1[0] > v2[0] ? v1[0] : v2[0], 
            v1[1] > v2[1] ? v1[1] : v2[1],
            v1[2] > v2[2] ? v1[2] : v2[2],
            v1[3] > v2[3] ? v1[3] : v2[3]
        };
    else
    {
        T output;
        for (size_t i = 0; i < T::rank(); i++)
            output[i] = v1[i] > v2[i] ? v1[i] : v2[i];
        return output;
    }
}

template <Vector T>
inline auto sum (const T& v) noexcept -> typename T::field
{
    typename T::field res = 0;
    for (int i = 0; i < v.rank(); i++)
        res += v.data[i];
    return res;
}

template <Vector T>
inline auto dot (const T& v1, const T& v2) noexcept -> typename T::field
{
    if constexpr (T::rank() == 2)
        return v1[0] * v2[0] + v1[1] * v2[1];
    else if constexpr (T::rank() == 3)
        return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
    else if constexpr (T::rank() == 4)
        return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2] + v1[3] * v2[3];
    else
        return sum(v1 * v2);
}

template <Vector T>
inline auto length_sq (const T& v1) noexcept -> typename T::field
{
    if constexpr (T::rank() == 2)
        return v1[0] * v1[0] + v1[1] * v1[1];
    else if constexpr (T::rank() == 3)
        return v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
    else if constexpr (T::rank() == 4)
        return v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2] + v1[3] * v1[3];
    else
        return sum(v1 * v1);
}

template<RealVector T> 
inline auto length (const T& v1) noexcept -> typename T::field
{
    if constexpr (T::rank() == 2)
        return std::sqrt(v1[0] * v1[0] + v1[1] * v1[1]);
    else if constexpr (T::rank() == 3)
        return std::sqrt(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);
    else if constexpr (T::rank() == 4)
        return std::sqrt(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2] + v1[3] * v1[3]);
    else
        return std::sqrt(sum(v1 * v1));
}

template<RealVector T> 
inline auto pow (const T& v, const typename T::field& p) noexcept -> T
{
    if constexpr (T::rank() == 2)
        return 
        { 
            std::pow(v[0], p), 
            std::pow(v[1], p) 
        };
    else if constexpr (T::rank() == 3)
        return 
        { 
            std::pow(v[0], p), 
            std::pow(v[1], p),
            std::pow(v[2], p)
        };
    else if constexpr (T::rank() == 4)
        return 
        { 
            std::pow(v[0], p), 
            std::pow(v[1], p),
            std::pow(v[2], p),
            std::pow(v[3], p)
        };
    else
    {
        T output;
        for (size_t i = 0; i < T::rank(); i++)
            output[i] = std::pow(v[i], p);
        return output;
    }
}

template<RealVector T> 
inline auto norm (const T& v) noexcept -> T
{
    auto s = 1 / length(v);
    return v * s;
}

template<RealVector T> 
inline auto norm (const T& input, T& output, typename T::field epsilon) noexcept -> bool
{
    auto l = length(input);
    if (l < epsilon)
        return false;
    auto s = 1 / length(input);
    output = input * s;
    return true;
}

template <Field S>
inline auto cross(const vec<S, 3>& v1, const vec<S, 3> v2) noexcept -> vec<S, 3>
{
    return 
    {
        v1[1] * v2[2] - v1[2] * v2[1],
        - v1[0] * v2[2] + v1[2] * v2[0],
        v1[0] * v2[1] - v1[1] * v2[0]
    };
}

template <Field S>
inline auto cross(const vec<S, 2>& v1, const vec<S, 2> v2) noexcept -> S
{
    return v1[0] * v2[1] - v1[1] * v2[0];
}

inline auto packUnorm4x8(const go::vu4& color) noexcept -> go::u32
{
    union { go::u8 in[4]; go::u32 out; } u = 
    {
        .in = {go::u8(color[0]), go::u8(color[1]), go::u8(color[2]), go::u8(color[3])}
    };

    return u.out;
}

// Column-major matrix
// r - row count (height)
// c - column count (width)
template <Field T, size_t r, size_t c>
struct mat
{
    using type = mat<T, r, c>;
    using type_transposed = mat<T, c, r>;
    using field = T;
    constexpr static auto rank() -> const vec<size_t, 2>  { return {r, c}; }
    constexpr static auto issquare() -> const bool { return r == c; }

    vec<T, r> data[c]; 

    inline auto operator[](size_t i) -> vec<T, r>& { return data[i]; }
    inline auto operator[](size_t i) const -> const vec<T, r>& { return data[i]; }

    inline auto operator+(const type& m) const -> type
    {
        type res;
        for (size_t i = 0; i < c; i++)
            res.data[i] = data[i] + m.data[i];
        return res;
    }
    inline auto operator-(const type& m) const -> type
    {
        type res;
        for (size_t i = 0; i < c; i++)
            res.data[i] = data[i] - m.data[i];
        return res;
    }
    inline auto operator*(const T& s) const -> type
    {
        type res;
        for (size_t i = 0; i < c; i++)
            res.data[i] = data[i] * s;
        return res;
    }

    template <size_t OUT_R, size_t OUT_C>
        requires (rank()[0] > OUT_R && rank()[1] > OUT_C)
    inline auto shrink() const noexcept -> mat<field, OUT_R, OUT_C>
    {
        if constexpr (OUT_R == OUT_C && OUT_R == 2)
            return { data[0].template shrink<OUT_R>(), data[1].template shrink<OUT_R>() };
        if constexpr (OUT_R == OUT_C && OUT_R == 3)
            return 
            { 
                data[0].template shrink<OUT_R>(), data[1].template shrink<OUT_R>(), 
                data[2].template shrink<OUT_R>()
            };
        if constexpr (OUT_R == OUT_C && OUT_R == 4)
            return 
            { 
                data[0].template shrink<OUT_R>(), data[1].template shrink<OUT_R>(), 
                data[2].template shrink<OUT_R>(), data[3].template shrink<OUT_R>()
            };
        else
        {
            mat<field, OUT_R, OUT_C> out;
            for (size_t i = 0; i < OUT_C; i++)
                out[i] = data[i].template shrink<OUT_R>();
            return out;
        }
    }

    static inline auto zero() noexcept -> mat<T, r, c> { return {0}; }

    static inline auto id() noexcept -> mat<T, c, c>
    {
        if constexpr (c == 2)
            return 
            {
                1, 0,
                0, 1,
            };
        else if constexpr (c == 3)
            return 
            {
                1, 0, 0,
                0, 1, 0,
                0, 0, 1,
            };
        else if constexpr (c == 4)
            return 
            {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
        else
        {
            auto result = mat<T, c, c>::zero();
            for (size_t i = 0; i < c; i++)
                result[c][c] = static_cast<T>(1);
            return result;
        }
    }
};

using mf2 = mat<f32, 2, 2>; using mf3 = mat<f32, 3, 3>; using mf4 = mat<f32, 4, 4>;
using md2 = mat<f64, 2, 2>; using md3 = mat<f64, 3, 3>; using md4 = mat<f64, 4, 4>;

template <Field S, size_t r, size_t c>
inline auto transpose(const mat<S, r, c>& m) noexcept -> mat<S, c, r>
{
    mat<S, c, r> res;
    for (int col = 0; col < c; col++)
        for (int row = 0; row < r; row++)
            res[row][col] = m.data[col][row];
    return res;
}

template <Field S, size_t r, size_t c, size_t c2>
inline auto operator*(const mat<S, r, c>& m1, const mat<S, c, c2>& m2) noexcept -> mat<S, r, c2>
{
    mat<S, r, c2> res;
    mat<S, c, r> transp = transpose(m1);
    for (size_t col = 0; col < c2; col++)
        for (size_t row = 0; row < r; row++)
            res[col][row] = dot(transp.data[row], m2.data[col]);
    return res;
}

template <Field S, size_t c>
inline auto operator*(const mat<S, c, c>& m, const vec<S, c>& v) noexcept -> vec<S, c>
{
    if constexpr (c == 2)
        return m[0] * v[0] + m[1] * v[1];
    else if constexpr(c == 3)
        return m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    else if constexpr(c == 4)
        return m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3] * v[3];
    else
    {
        vec<S, c> result = {};
        for (size_t i = 0; i < c; i++)
            result = result + m[i] * v[i];
        return result;
    }
}

template<Field S>
inline auto det(const mat<S, 2, 2>& m) noexcept -> S
{
    return m[0][0] * m[1][1] - m[1][0] * m[0][1];
}

template<Field S>
inline auto det(const mat<S, 3, 3>& m) noexcept -> S
{
    return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) 
         - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) 
         + m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}

template<RealField S>
inline auto translate(const vec<S, 3>& v) noexcept -> mat<S, 4, 4>
{
    return 
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        v.data[0], v.data[1], v.data[2], 1
    };
}

// Axis must be normalized, angle in radians
template<RealField S>
inline auto rot4(const vec<S, 3>& axis, S angle) noexcept -> mat<S, 4, 4>
{
    const S c = std::cos(angle);
    const S s = std::sin(angle);

    auto term = (1 - c) * axis;

    mat<S, 4, 4> output;
    output[0][0] = term[0] * axis[0] + c;
    output[0][1] = term[0] * axis[1] + s * axis[2];
    output[0][2] = term[0] * axis[2] - s * axis[1];
    output[0][3] = 0;

    output[1][0] = term[1] * axis[0] - s * axis[2];
    output[1][1] = term[1] * axis[1] + c;
    output[1][2] = term[1] * axis[2] + s * axis[0];
    output[1][3] = 0;

    output[2][0] = term[2] * axis[0] + s * axis[1];
    output[2][1] = term[2] * axis[1] - s * axis[0];
    output[2][2] = term[2] * axis[2] + c;
    output[2][3] = 0;

    output[3] = vec<S, 4>{0, 0, 0, 1};

    return output;
}

template<RealField S>
inline auto rot3(const vec<S, 3>& axis, S angle) noexcept -> mat<S, 3, 3>
{
    const S c = std::cos(angle);
    const S s = std::sin(angle);

    auto term = (1 - c) * axis;

    mat<S, 3, 3> output;
    output[0][0] = term[0] * axis[0] + c;
    output[0][1] = term[0] * axis[1] + s * axis[2];
    output[0][2] = term[0] * axis[2] - s * axis[1];

    output[1][0] = term[1] * axis[0] - s * axis[2];
    output[1][1] = term[1] * axis[1] + c;
    output[1][2] = term[1] * axis[2] + s * axis[0];

    output[2][0] = term[2] * axis[0] + s * axis[1];
    output[2][1] = term[2] * axis[1] - s * axis[0];
    output[2][2] = term[2] * axis[2] + c;

    return output;
}

template<RealField S>
inline auto ortho_proj_rh(S width, S height, S nearZ, S farZ) noexcept -> mat<S, 4, 4>
{
    auto output = mat<S, 4, 4>::zero();
    output.data[0][0] = 2 / width;
    output.data[1][1] = -2 / height;
    output.data[2][2] = - 1 / (farZ - nearZ);
    output.data[3][2] = nearZ * output.data[2][2];
    output.data[3][3] = 1;
    return output;
}

// Perspective projection for NDC(x : [-1, 1]; y [-1, 1]; z : [0, 1])
// RHS(x : right; y : down; z : from us) -> RHS(x: right; y : up; z : on us)
// So basicaly this is a transformation from vulkan ndc to opengl view conventional coorinate system,
// or other isomorphic transformations.
// In glm library it is glm::perspectiveRH_ZO with flliped [1][1] element.
template<RealField S>
inline auto persp_proj_rh(S verticalFov, S width, S height, S nearZ, S farZ) noexcept -> mat<S, 4, 4>
{
    S f =  static_cast<S>(1.0) / std::tan(verticalFov * static_cast<S>(0.5));
    S near_far = 1 / (nearZ - farZ);

    auto output = mat<S, 4, 4>::zero();

    output[0][0] = f * height / width;
    output[1][1] = -f;
    output[2][2] = farZ * near_far;
    output[2][3] = static_cast<S>(-1);
    output[3][2] = (farZ * nearZ) * near_far;
    return output;
}

// Perspective projection with reverse-Z for NDC(x: [-1,1]; y: [-1,1]; z: [0,1])
// RHS(x: right; y: down; z: from us) -> RHS(x: right; y: up; z: on us)
// Reverse-Z: near plane → z=1, far plane → z=0
// Use VK_COMPARE_OP_GREATER_OR_EQUAL and clear depth to 0.0
template<RealField S>
inline auto persp_proj_rh_reverse_z(S verticalFov, S width, S height, S nearZ, S farZ) noexcept -> mat<S, 4, 4>
{
    S f = static_cast<S>(1.0) / std::tan(verticalFov * static_cast<S>(0.5));
    S near_far = 1 / (nearZ - farZ);
    auto output = mat<S, 4, 4>::zero();
    output[0][0] = f * height / width;
    output[1][1] = -f;
    output[2][2] = nearZ * near_far;
    output[2][3] = static_cast<S>(-1);
    output[3][2] = -(farZ * nearZ) * near_far;
    return output;
}

// Infinite reverse-Z (recommended for best precision)
template<RealField S>
inline auto persp_proj_rh_reverse_z_infinite(S verticalFov, S width, S height, S nearZ) noexcept -> mat<S, 4, 4>
{
    S f = static_cast<S>(1.0) / std::tan(verticalFov * static_cast<S>(0.5));
    auto output = mat<S, 4, 4>::zero();
    output[0][0] = f * height / width;
    output[1][1] = -f;
    output[2][2] = static_cast<S>(0);
    output[2][3] = static_cast<S>(-1);
    output[3][2] = nearZ;
    return output;
}

template<RealField S>
inline auto look_at_rh(const vec<S, 3>& pos, const vec<S, 3>& target, const vec<S, 3>& up) noexcept -> mat<S, 4, 4>
{
    mat<S, 4, 4> output;

    vec<S, 3> f = norm(target - pos);
    vec<S, 3> s = norm(cross(f, up));
    vec<S, 3> u = cross(s, f);

    output[0][0] = s[0]; output[0][1] = u[0]; output[0][2] = -f[0]; output[0][3] = 0;
    output[1][0] = s[1]; output[1][1] = u[1]; output[1][2] = -f[1]; output[1][3] = 0;
    output[2][0] = s[2]; output[2][1] = u[2]; output[2][2] = -f[2]; output[2][3] = 0;

    output[3][0] = -dot(s, pos);
    output[3][1] = -dot(u, pos);
    output[3][2] = dot(f, pos);
    output[3][3] = 1;
    return output;
}

template<RealField S>
inline auto look_at_lh(const vec<S, 3>& pos, const vec<S, 3>& target, const vec<S, 3>& up) noexcept -> mat<S, 4, 4>
{
    mat<S, 4, 4> output;

    vec<S, 3> f = norm(target - pos);
    vec<S, 3> s = norm(cross(up, f));
    vec<S, 3> u = cross(f, s);

    output[0][0] = s[0]; output[0][1] = u[0]; output[0][2] = f[0]; output[0][3] = 0;
    output[1][0] = s[1]; output[1][1] = u[1]; output[1][2] = f[1]; output[1][3] = 0;
    output[2][0] = s[2]; output[2][1] = u[2]; output[2][2] = f[2]; output[2][3] = 0;
    
    output[3][0] = -dot(s, pos); 
    output[3][1] = -dot(u, pos); 
    output[3][2] = -dot(f, pos);
    output[3][3] = 1;
    return output;
}

template<Vector T>
inline auto lerp(const T& a, const T& b, typename T::field t) noexcept -> T
{
    return a * (static_cast<typename T::field>(1) - t) + b * t;
}

template<Vector T>
inline auto lerp(const T& a, const T& b, const T& t) noexcept -> T
{
    return a * (T::from(static_cast<typename T::field>(1)) - t) + b * t;
}

template <Vector T>
inline auto clamp(const T& v, const T& minVal, const T& maxVal) noexcept -> T
{
    return go::min(go::max(v, minVal), maxVal);
}

// step
template<Vector T>
inline auto step(const T& edge, const T& x) noexcept -> T
{
    if constexpr (T::rank() == 2)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge[0]),
            static_cast<typename T::field>(x[1] >= edge[1]) 
        };
    else if constexpr (T::rank() == 3)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge[0]),
            static_cast<typename T::field>(x[1] >= edge[1]),
            static_cast<typename T::field>(x[2] >= edge[2])
        };
    else if constexpr (T::rank() == 4)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge[0]),
            static_cast<typename T::field>(x[1] >= edge[1]),
            static_cast<typename T::field>(x[2] >= edge[2]),
            static_cast<typename T::field>(x[3] >= edge[3])
        };
    else
    {
        T output;
        for (size_t i = 0; i < T::rank(); i++)
            output[i] = static_cast<typename T::field>(x[i] >= edge[i]);
        return output;
    }
}

template<Vector T>
inline auto step(typename T::field edge, const T& x) noexcept -> T
{
    if constexpr (T::rank() == 2)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge),
            static_cast<typename T::field>(x[1] >= edge) 
        };
    else if constexpr (T::rank() == 3)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge),
            static_cast<typename T::field>(x[1] >= edge),
            static_cast<typename T::field>(x[2] >= edge)
        };
    else if constexpr (T::rank() == 4)
        return 
        { 
            static_cast<typename T::field>(x[0] >= edge),
            static_cast<typename T::field>(x[1] >= edge),
            static_cast<typename T::field>(x[2] >= edge),
            static_cast<typename T::field>(x[3] >= edge)
        };
    else
    {
        T output;
        for (size_t i = 0; i < T::rank(); i++)
            output[i] = static_cast<typename T::field>(x[i] >= edge);
        return output;
    }
}

template<Vector T>
inline auto linmap(const T& src, const T& srcStart, const T& srcEnd, 
        const T& dstStart, const T& dstEnd) noexcept -> T
{
    // (src - srcStart) / (srcEnd - srcStart) == (dst - dstStart) / (dstEnd - dstStart); =>
    return ((dstEnd - dstStart)*(src - srcStart))/(srcEnd - srcStart) + dstStart;
}

// TODO: consider to use memcpy
template<Field S, size_t c, typename ...Args>
inline auto grow(const vec<S, c>& v, Args... args) noexcept -> vec<S, c + sizeof...(Args)>
{
    using Output = vec<S, c + sizeof...(Args)>;
    if constexpr(c == 2)
        return {v[0], v[1], args...};
    else if constexpr (c == 3)
        return {v[0], v[1], v[2], args...};
    else if constexpr (c == 4)
        return {v[0], v[1], v[2], v[3], args...};
    else
    {
        Output output;
        for (size_t i = 0; i < c; i++)
            output[i] = v[i];
        S data[Output::rank()] = {args...};
        for (size_t i = c; i < Output::rank(); i++)
            output[i] = data[i - c];
        return output;
    }
}

template <RealField S, size_t c>
struct aabb
{
    using field = S;

    vec<S, c> min;
    vec<S, c> max;

    inline auto center() const noexcept -> vec<S, c>
    {
        return (max + min) * static_cast<S>(0.5);
    }
    inline auto size() const noexcept -> vec<S, c>
    {
        return max - min;
    }

    auto operator+(const aabb& other) const noexcept -> aabb
    {
        return 
        {
            .min = go::min(min, other.min),
            .max = go::max(max, other.max)
        };
    }

    static auto invalid() noexcept -> aabb
    {
        if constexpr (c == 2)
            return 
            {
                .min = 
                { 
                    std::numeric_limits<S>::max(),  
                    std::numeric_limits<S>::max(),  
                },
                .max = 
                { 
                    std::numeric_limits<S>::lowest(), 
                    std::numeric_limits<S>::lowest(), 
                },
            };
        else if constexpr(c == 3)
            return 
            {
                .min = 
                { 
                    std::numeric_limits<S>::max(),  
                    std::numeric_limits<S>::max(),  
                    std::numeric_limits<S>::max()
                },
                .max = 
                { 
                    std::numeric_limits<S>::lowest(), 
                    std::numeric_limits<S>::lowest(), 
                    std::numeric_limits<S>::lowest()
                },
            };
        else
        {
            aabb output;
            for (size_t i = 0; i < c; i++)
            {
                output.min[i] = std::numeric_limits<S>::max();
                output.max[i] = std::numeric_limits<S>::lowest();
            }
            return output;
        }
    }
};

using aabbf2 = aabb<float, 2>;
using aabbd2 = aabb<double, 2>;
using aabbf3 = aabb<float, 3>;
using aabbd3 = aabb<double, 3>;

template <RealField S, size_t c>
inline auto enlarge(const aabb<S, c>& bb, const vec<S, c>& v) noexcept -> aabb<S, c>
{
    return 
    {
        .min = go::min(bb.min, v),
        .max = go::max(bb.max, v)
    };
}

// Color helpers

template<RealField T> 
inline auto srgb_to_linear(const vec<T, 3>& v) noexcept -> vec<T, 3>
{
    constexpr T a = static_cast<T>(0.055);
    constexpr T threshold = static_cast<T>(0.04045);
    constexpr T invGamma = static_cast<T>(2.4);
    constexpr T linearScale = static_cast<T>(0.07739938080495357); // 1.0f / 12.92f
    constexpr T gammaScale = static_cast<T>(0.9478672985781991); // 1.0f / 1.055f
    return lerp(v * linearScale, pow((v + vec<T, 3>::from(a)) * gammaScale, invGamma), step(threshold, v));
}

template<RealField T>
inline auto srgba_to_linear(const vec<T, 4>& v) noexcept -> vec<T, 4>
{
    return go::grow(go::srgb_to_linear(v.template shrink<3>()), v[3]);
}

template<RealField T> 
inline auto srgb_to_linear_simple(const vec<T, 3>& v) noexcept -> vec<T, 3>
{
    return pow(v, static_cast<T>(2.2));
}

template<RealField T>
inline auto srgba_to_linear_simple(const vec<T, 4>& v) noexcept -> vec<T, 4>
{
    return go::grow(go::srgb_to_linear_simple(v.template shrink<3>()), v[3]);
}
}

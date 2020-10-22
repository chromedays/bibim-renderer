#pragma once
#include "util.h"
#include <limits>

namespace bb {
constexpr float pi32 = 3.141592f;
constexpr float halfPi32 = pi32 * 0.5f;
constexpr float twoPi32 = pi32 * 2.f;
constexpr float epsilon32 = std::numeric_limits<float>::epsilon();

int compareFloats(float _a, float _b);
inline float degToRad(float _degrees) { return _degrees * pi32 / 180.f; }
inline float radToDeg(float _radians) { return _radians * 180.f / pi32; }

struct Int2 {
  int X = 0;
  int Y = 0;

  Int2 operator-(Int2 _other) const { return {X - _other.X, Y - _other.Y}; }
};

struct Float2 {
  float X = 0.f;
  float Y = 0.f;

  Float2 operator-(Float2 _other) const { return {X - _other.X, Y - _other.Y}; }
};

inline float dot(Float2 _a, Float2 _b) { return _a.X * _b.X + _a.Y * _b.Y; }

struct Float3 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;

  float lengthSq() const;
  float length() const;
  Float3 normalize() const;
  Float3 operator+(const Float3 &_other) const;
  Float3 operator-(const Float3 &_other) const;
  Float3 operator*(float _multiplier) const;
  Float3 operator/(float _divider) const;
  const Float3 &operator+=(const Float3 &_other);
};

float dot(const Float3 &_a, const Float3 &_b);
Float3 cross(const Float3 &_a, const Float3 &_b);

struct Float4 {
  float X = 0.f;
  float Y = 0.f;
  float Z = 0.f;
  float W = 0.f;
};

float dot(const Float4 &_a, const Float4 &_b);

struct Mat3 {
  float M[3][3] = {};

  float determinant() const;
};

struct Mat4 {
  float M[4][4] = {};

  Float4 row(int _n) const;
  Float4 column(int _n) const;
  float cofactor(int _row, int _col) const;
  Mat4 inverse() const;
  Mat4 transpose() const;

  static Mat4 identity();
  static Mat4 translate(const Float3 &_delta);
  static Mat4 scale(const Float3 &_scale);
  static Mat4 scale(float _scale);
  static Mat4 rotateX(float _degrees);
  static Mat4 rotateY(float _degrees);
  static Mat4 rotateZ(float _degrees);
  static Mat4 lookAt(const Float3 &_eye, const Float3 &_target,
                     const Float3 &_upAxis = {0, 1, 0});
  static Mat4 perspective(float _fovDegrees, float _aspectRatio, float _nearZ,
                          float _farZ);
};

Mat4 operator*(const Mat4 &_a, const Mat4 &_b);
Mat4 operator/(const Mat4 &_a, float _b);

struct SphericalFloat3 {
  float r;
  float theta;
  float phi;
};

Float3 sphericalToCartesian(const SphericalFloat3 &_spherical);

} // namespace bb
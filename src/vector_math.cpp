#include "vector_math.h"

namespace bb {

int compareFloats(float a, float b) {
  float diff = a - b;
  if (diff < 0) {
    diff = -diff;
  }

  if (diff <= epsilon32) {
    return 0;
  } else if (a > b) {
    return -1;
  } else {
    return 1;
  }
}

float Float3::lengthSq() const {
  float result = X * X + Y * Y + Z * Z;
  return result;
}

float Float3::length() const {
  float result = sqrtf(lengthSq());
  return result;
}

Float3 Float3::normalize() const {
  float len = length();
  Float3 result = *this / len;
  return result;
}

Float3 Float3::operator+(const Float3 &_other) const {
  Float3 result = {X + _other.X, Y + _other.Y, Z + _other.Z};
  return result;
}

Float3 Float3::operator-(const Float3 &_other) const {
  Float3 result = {X - _other.X, Y - _other.Y, Z - _other.Z};
  return result;
}

Float3 Float3::operator*(float _multiplier) const {
  Float3 result = {X * _multiplier, Y * _multiplier, Z * _multiplier};
  return result;
}

Float3 Float3::operator/(float _divider) const {
  Float3 result = {X / _divider, Y / _divider, Z / _divider};
  return result;
}

const Float3 &Float3::operator+=(const Float3 &_other) {
  *this = *this + _other;
  return *this;
}

float dot(const Float3 &_a, const Float3 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z;
}
Float3 cross(const Float3 &_a, const Float3 &_b) {
  Float3 result = {
      _a.Y * _b.Z - _a.Z * _b.Y,
      _a.Z * _b.X - _a.X * _b.Z,
      _a.X * _b.Y - _a.Y * _b.X,
  };
  return result;
}

float dot(const Float4 &_a, const Float4 &_b) {
  return _a.X * _b.X + _a.Y * _b.Y + _a.Z * _b.Z + _a.W * _b.W;
}

float Mat3::determinant() const {
  float result = M[0][0] * (M[1][1] * M[2][2] - M[2][1] * M[1][2]) -
                 M[1][0] * (M[0][1] * M[2][2] - M[2][1] * M[0][2]) +
                 M[2][0] * (M[0][1] * M[1][2] - M[1][1] * M[0][2]);
  return result;
}

Float4 Mat4::row(int _n) const {
  BB_ASSERT(_n >= 0 && _n < 4);
  return {M[0][_n], M[1][_n], M[2][_n], M[3][_n]};
}

Float4 Mat4::column(int _n) const {
  BB_ASSERT(_n >= 0 && _n < 4);
  return {M[_n][0], M[_n][1], M[_n][2], M[_n][3]};
}

float Mat4::cofactor(int _row, int _col) const {
  Mat3 minor;
  int minorRow = 0;
  int minorCol = 0;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (r != _row && c != _col) {
        minor.M[minorCol][minorRow++] = M[c][r];
        if (minorRow == 3) {
          minorRow = 0;
          ++minorCol;
        }
      }
    }
  }

  float sign = ((_row + _col) % 2) ? -1.f : 1.f;
  float result = minor.determinant() * sign;
  return result;
}

Mat4 Mat4::inverse() const {
  Mat4 adjoint;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      adjoint.M[c][r] = cofactor(r, c);
    }
  }

  float det = 0.f;
  for (int i = 0; i < 4; ++i) {
    det += M[i][0] * adjoint.M[i][0];
  }

  BB_ASSERT(compareFloats(det, 0.f) != 0);

  adjoint = adjoint.transpose();

  Mat4 result = adjoint / det;
  return result;
}

Mat4 Mat4::transpose() const {
  Mat4 transposed;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      transposed.M[c][r] = M[r][c];
    }
  }

  return transposed;
}

Mat4 Mat4::identity() {
  return {{
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1},
  }};
}

Mat4 Mat4::translate(const Float3 &_delta) {
  // clang-format off
    return {{
      {1, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {_delta.X, _delta.Y, _delta.Z, 1},
    }};
  // clang-format on
}

Mat4 Mat4::scale(const Float3 &_scale) {
  // clang-format off
    return {{
      {_scale.X, 0, 0, 0},
      {0, _scale.Y, 0, 0},
      {0, 0, _scale.Z, 0},
      {0, 0, 0, 1},
    }};
  // clang-format on
}

Mat4 Mat4::scale(float _scale) {
  // clang-format off
    return {{
      {_scale, 0, 0, 0},
      {0, _scale, 0, 0},
      {0, 0, _scale, 0},
      {0, 0, 0, 1},
    }};
  // clang-format on
}

Mat4 Mat4::rotateX(float _degrees) {
  float radians = degToRad(_degrees);
  float cr = cosf(radians);
  float sr = sinf(radians);
  // clang-format off
    return {{
      {1, 0,   0,  0},
      {0, cr,  sr, 0},
      {0, -sr, cr, 0},
      {0, 0,   0,  1},
    }};
  // clang-format on
};

Mat4 Mat4::rotateY(float _degrees) {
  float radians = degToRad(_degrees);
  float cr = cosf(radians);
  float sr = sinf(radians);
  // clang-format off
    return {{
      {cr,  0, sr, 0},
      {0,   1, 0,  0},
      {-sr, 0, cr, 0},
      {0,   0, 0,  1},
    }};
  // clang-format on
}

Mat4 Mat4::rotateZ(float _degrees) {
  float radians = degToRad(_degrees);
  float cr = cosf(radians);
  float sr = sinf(radians);
  // clang-format off
    return {{
      {cr,  sr, 0, 0},
      {-sr, cr, 0, 0},
      {0,   0,  1, 0},
      {0,   0,  0, 1},
    }};
  // clang-format on
}

Mat4 Mat4::lookAt(const Float3 &_eye, const Float3 &_target,
                  const Float3 &_upAxis) {
  Float3 forward = (_target - _eye).normalize();
  Float3 right = cross(_upAxis, forward).normalize();
  Float3 up = cross(forward, right).normalize();

  // clang-format off
    return {{
      {right.X, up.X, forward.X, 0},
      {right.Y, up.Y, forward.Y, 0},
      {right.Z, up.Z, forward.Z, 0},
      {-dot(_eye, right), -dot(_eye, up), -dot(_eye, forward), 1},
    }};
  // clang-format on
}

Mat4 Mat4::perspective(float _fovDegrees, float aspectRatio, float _nearZ,
                       float _farZ) {
  float d = 1.f / tan(degToRad(_fovDegrees) * 0.5f);
  float fSubN = _farZ - _nearZ;
  // clang-format off
    Mat4 result = {{
      {d / aspectRatio, 0, 0,                    0},
      {0,               -d, 0,                    0},
      {0,               0, -_nearZ / fSubN,       1},
      {0,               0, _nearZ * _farZ / fSubN, 0},
    }};
  // clang-format on
  return result;
}

Mat4 operator*(const Mat4 &_a, const Mat4 &_b) {
  Float4 rows[4] = {_a.row(0), _a.row(1), _a.row(2), _a.row(3)};
  Float4 columns[4] = {_b.column(0), _b.column(1), _b.column(2), _b.column(3)};
  Mat4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.M[j][i] = dot(rows[i], columns[j]);
    }
  }
  return result;
}

Mat4 operator/(const Mat4 &_a, float b) {
  Mat4 result = _a;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      result.M[c][r] /= b;
    }
  }
  return result;
}

} // namespace bb
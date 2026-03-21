#pragma once

#include <cmath>

namespace BaseballSimulatorVR::Physics
{
	struct Vector3 final
	{
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };
	};

	[[nodiscard]] inline Vector3 operator+(const Vector3& left, const Vector3& right)
	{
		return Vector3{ left.x + right.x, left.y + right.y, left.z + right.z };
	}

	[[nodiscard]] inline Vector3 operator-(const Vector3& left, const Vector3& right)
	{
		return Vector3{ left.x - right.x, left.y - right.y, left.z - right.z };
	}

	[[nodiscard]] inline Vector3 operator-(const Vector3& value)
	{
		return Vector3{ -value.x, -value.y, -value.z };
	}

	[[nodiscard]] inline Vector3 operator*(const Vector3& value, double scalar)
	{
		return Vector3{ value.x * scalar, value.y * scalar, value.z * scalar };
	}

	[[nodiscard]] inline Vector3 operator*(double scalar, const Vector3& value)
	{
		return value * scalar;
	}

	[[nodiscard]] inline Vector3 operator/(const Vector3& value, double scalar)
	{
		return Vector3{ value.x / scalar, value.y / scalar, value.z / scalar };
	}

	inline Vector3& operator+=(Vector3& left, const Vector3& right)
	{
		left.x += right.x;
		left.y += right.y;
		left.z += right.z;
		return left;
	}

	inline Vector3& operator-=(Vector3& left, const Vector3& right)
	{
		left.x -= right.x;
		left.y -= right.y;
		left.z -= right.z;
		return left;
	}

	inline Vector3& operator*=(Vector3& value, double scalar)
	{
		value.x *= scalar;
		value.y *= scalar;
		value.z *= scalar;
		return value;
	}

	[[nodiscard]] inline double Dot(const Vector3& left, const Vector3& right)
	{
		return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
	}

	[[nodiscard]] inline Vector3 Cross(const Vector3& left, const Vector3& right)
	{
		return Vector3{
			(left.y * right.z) - (left.z * right.y),
			(left.z * right.x) - (left.x * right.z),
			(left.x * right.y) - (left.y * right.x),
		};
	}

	[[nodiscard]] inline double Length(const Vector3& value)
	{
		return std::sqrt(Dot(value, value));
	}

	[[nodiscard]] inline double LengthSquared(const Vector3& value)
	{
		return Dot(value, value);
	}

	[[nodiscard]] inline Vector3 NormalizeOrZero(const Vector3& value)
	{
		constexpr double epsilon = 1e-9;
		const double magnitude = Length(value);
		if (magnitude <= epsilon)
		{
			return Vector3{};
		}

		return value / magnitude;
	}
} // namespace BaseballSimulatorVR::Physics

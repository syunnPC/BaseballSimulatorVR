#pragma once

#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace BaseballSimulatorVR::Common
{
	struct BatOrientation final
	{
		Physics::Vector3 axis{};
		Physics::Vector3 up{};
	};

	[[nodiscard]] inline std::optional<BatOrientation> BuildBatOrientation(
	    const Physics::Vector3& forward, const Physics::Vector3& up,
	    double angleOffsetDegrees, double yawOffsetDegrees) noexcept
	{
		const auto finite = [](const Physics::Vector3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		if (!finite(forward) || !finite(up) || !std::isfinite(angleOffsetDegrees) || !std::isfinite(yawOffsetDegrees))
			return std::nullopt;
		Physics::Vector3 direction = Physics::NormalizeOrZero(forward);
		Physics::Vector3 upAxis = Physics::NormalizeOrZero(up);
		if (Physics::LengthSquared(direction) < 0.5 || Physics::LengthSquared(upAxis) < 0.5 ||
		    std::abs(Physics::Dot(direction, upAxis)) > 0.995)
			return std::nullopt;
		upAxis = Physics::NormalizeOrZero(upAxis - direction * Physics::Dot(direction, upAxis));
		const auto rotate = [](const Physics::Vector3& vector, const Physics::Vector3& axis, double degrees)
		{
			const double angle = std::remainder(degrees, 360.0) * std::numbers::pi / 180.0;
			return vector * std::cos(angle) + Physics::Cross(axis, vector) * std::sin(angle) +
			    axis * (Physics::Dot(axis, vector) * (1.0 - std::cos(angle)));
		};
		direction = rotate(direction, upAxis, yawOffsetDegrees);
		const Physics::Vector3 rightAxis = Physics::NormalizeOrZero(Physics::Cross(direction, upAxis));
		return BatOrientation{
			Physics::NormalizeOrZero(rotate(direction, rightAxis, angleOffsetDegrees)),
			Physics::NormalizeOrZero(rotate(upAxis, rightAxis, angleOffsetDegrees))
		};
	}

	[[nodiscard]] inline std::optional<Physics::Vector3> BuildBatAxisDirection(
	    const Physics::Vector3& forward, const Physics::Vector3& up,
	    double angleOffsetDegrees, double yawOffsetDegrees) noexcept
	{
		const auto orientation = BuildBatOrientation(forward, up, angleOffsetDegrees, yawOffsetDegrees);
		if (!orientation) return std::nullopt;
		return orientation->axis;
	}

	// Rotates a vector about a (not necessarily unit) rotation vector whose length is the angle.
	[[nodiscard]] inline Physics::Vector3 RotateByRotationVector(
	    const Physics::Vector3& vector, const Physics::Vector3& rotation) noexcept
	{
		const double angle = Physics::Length(rotation);
		if (!(angle > 1e-12) || !std::isfinite(angle)) return vector;
		const Physics::Vector3 axis = rotation / angle;
		return vector * std::cos(angle) + Physics::Cross(axis, vector) * std::sin(angle) +
		    axis * (Physics::Dot(axis, vector) * (1.0 - std::cos(angle)));
	}

	// The bat frame part-way through a constant-angular-velocity rotation.
	[[nodiscard]] inline BatOrientation RotateBatOrientation(
	    const BatOrientation& orientation, const Physics::Vector3& angularVelocity, double seconds) noexcept
	{
		const Physics::Vector3 rotation = angularVelocity * seconds;
		return BatOrientation{
			Physics::NormalizeOrZero(RotateByRotationVector(orientation.axis, rotation)),
			Physics::NormalizeOrZero(RotateByRotationVector(orientation.up, rotation))
		};
	}

	// Rotation of the complete controller frame, including roll about the bat
	// axis. Axis endpoints alone cannot observe this surface velocity component.
	[[nodiscard]] inline std::optional<Physics::Vector3> ComputeBatAngularVelocity(
	    const BatOrientation& previous, const BatOrientation& current, double seconds) noexcept
	{
		if (!(seconds > 0.0) || !std::isfinite(seconds)) return std::nullopt;
		const auto previousRight = Physics::Cross(previous.axis, previous.up);
		const auto currentRight = Physics::Cross(current.axis, current.up);
		const double cosine = std::clamp((Physics::Dot(previous.axis, current.axis) +
		    Physics::Dot(previous.up, current.up) + Physics::Dot(previousRight, currentRight) - 1.0) * 0.5,
		    -1.0, 1.0);
		const double angle = std::acos(cosine);
		if (!std::isfinite(angle) || angle >= std::numbers::pi - 1e-6) return std::nullopt;
		const auto twiceSineAxis = Physics::Cross(previous.axis, current.axis) +
		    Physics::Cross(previous.up, current.up) + Physics::Cross(previousRight, currentRight);
		const double factor = angle < 1e-6 ? 0.5 / seconds : angle / (2.0 * std::sin(angle) * seconds);
		return twiceSineAxis * factor;
	}
}

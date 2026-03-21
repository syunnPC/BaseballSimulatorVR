#pragma once

#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Physics
{
	enum class PitchIntegratorType
	{
		SemiImplicitEuler = 0,
		Rk4 = 1,
	};

	[[nodiscard]] inline std::string_view GetPitchIntegratorToken(PitchIntegratorType integratorType) noexcept
	{
		switch (integratorType)
		{
			case PitchIntegratorType::SemiImplicitEuler:
				return "SemiImplicitEuler";
			case PitchIntegratorType::Rk4:
				return "Rk4";
			default:
				return "Rk4";
		}
	}

	[[nodiscard]] inline std::wstring_view GetPitchIntegratorName(PitchIntegratorType integratorType) noexcept
	{
		switch (integratorType)
		{
			case PitchIntegratorType::SemiImplicitEuler:
				return L"SemiImplicitEuler";
			case PitchIntegratorType::Rk4:
				return L"Rk4";
			default:
				return L"Unknown";
		}
	}
} // namespace BaseballSimulatorVR::Physics

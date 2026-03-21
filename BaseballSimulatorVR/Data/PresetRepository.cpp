#include "PresetRepository.hpp"

#include <array>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		[[nodiscard]] Baseball::PitchParameters BuildPreset(
		    const Physics::Vector3& initialVelocity,
		    const Physics::Vector3& spinAxis,
		    const double spinRpm,
		    const double dragCoefficient,
		    const double liftCoefficient,
		    const bool enableSeamPerturbation,
		    const double seamPerturbationAmplitude,
		    const double seamPerturbationFrequencyHz,
		    const double seamPerturbationPhase)
		{
			Baseball::PitchParameters parameters{};
			parameters.initialVelocity = initialVelocity;
			parameters.spinAxis = spinAxis;
			parameters.spinRpm = spinRpm;
			parameters.dragCoefficient = dragCoefficient;
			parameters.liftCoefficient = liftCoefficient;
			parameters.integratorType = Physics::PitchIntegratorType::Rk4;
			parameters.useCoefficientTable = true;
			parameters.enableHeightDependentWind = false;
			parameters.windShearFactor = 0.08;
			parameters.enableSeamPerturbation = enableSeamPerturbation;
			parameters.seamPerturbationAmplitude = seamPerturbationAmplitude;
			parameters.seamPerturbationFrequencyHz = seamPerturbationFrequencyHz;
			parameters.seamPerturbationPhase = seamPerturbationPhase;
			return parameters;
		}

		Baseball::PitchParameters BuildFastballPreset()
		{
			return BuildPreset(
			    Physics::Vector3{ 0.0, 0.0, 41.5 },
			    Physics::Vector3{ 0.0, 1.0, 0.0 },
			    2300.0,
			    0.00045,
			    0.00070,
			    false,
			    0.08,
			    22.0,
			    0.0);
		}

		Baseball::PitchParameters BuildSliderPreset()
		{
			return BuildPreset(
			    Physics::Vector3{ 0.0, 0.0, 37.5 },
			    Physics::Vector3{ 0.15, 0.90, 0.40 },
			    2400.0,
			    0.00048,
			    0.00062,
			    true,
			    0.06,
			    24.0,
			    0.5);
		}

		Baseball::PitchParameters BuildCurveballPreset()
		{
			return BuildPreset(
			    Physics::Vector3{ 0.0, 0.0, 34.0 },
			    Physics::Vector3{ 1.0, 0.0, 0.0 },
			    2700.0,
			    0.00050,
			    0.00095,
			    true,
			    0.10,
			    28.0,
			    1.2);
		}

		Baseball::PitchParameters BuildChangeupPreset()
		{
			return BuildPreset(
			    Physics::Vector3{ 0.0, 0.0, 31.0 },
			    Physics::Vector3{ 0.05, 0.95, -0.2 },
			    1650.0,
			    0.00047,
			    0.00056,
			    false,
			    0.05,
			    20.0,
			    0.0);
		}

		struct PresetDefinition final
		{
			PitchPresetType presetType{ PitchPresetType::Fastball };
			std::wstring_view presetName{};
			Baseball::PitchParameters pitchParameters{};
		};

		[[nodiscard]] const std::array<PresetDefinition, 4>&
		GetDefaultPresetDefinitions()
		{
			static const std::array<PresetDefinition, 4> definitions{
				PresetDefinition{
				    PitchPresetType::Fastball,
				    L"Fastball",
				    BuildFastballPreset() },
				PresetDefinition{
				    PitchPresetType::Slider,
				    L"Slider",
				    BuildSliderPreset() },
				PresetDefinition{
				    PitchPresetType::Curveball,
				    L"Curveball",
				    BuildCurveballPreset() },
				PresetDefinition{
				    PitchPresetType::Changeup,
				    L"Changeup",
				    BuildChangeupPreset() }
			};
			return definitions;
		}

		[[nodiscard]] const PresetDefinition* FindPresetDefinition(
		    const PitchPresetType presetType)
		{
			const auto& definitions = GetDefaultPresetDefinitions();
			for (const PresetDefinition& definition : definitions)
			{
				if (definition.presetType == presetType)
				{
					return &definition;
				}
			}
			return nullptr;
		}
	} // namespace

	Baseball::PitchParameters PresetRepository::LoadDefaultPitchParameters() const
	{
		const PresetDefinition* const defaultPreset =
		    FindPresetDefinition(PitchPresetType::Fastball);
		if (defaultPreset != nullptr)
		{
			return defaultPreset->pitchParameters;
		}
		return BuildFastballPreset();
	}

	Baseball::PitchParameters PresetRepository::LoadPitchParameters(PitchPresetType presetType) const
	{
		if (presetType == PitchPresetType::Custom)
		{
			return LoadDefaultPitchParameters();
		}

		const PresetDefinition* const presetDefinition =
		    FindPresetDefinition(presetType);
		if (presetDefinition != nullptr)
		{
			return presetDefinition->pitchParameters;
		}
		return LoadDefaultPitchParameters();
	}

	std::wstring_view PresetRepository::GetPresetName(PitchPresetType presetType) const noexcept
	{
		if (presetType == PitchPresetType::Custom)
		{
			return L"Custom";
		}

		const PresetDefinition* const presetDefinition =
		    FindPresetDefinition(presetType);
		if (presetDefinition != nullptr)
		{
			return presetDefinition->presetName;
		}
		return L"Unknown";
	}
} // namespace BaseballSimulatorVR::Data

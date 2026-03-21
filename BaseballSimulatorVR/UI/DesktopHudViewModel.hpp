#pragma once

#include "../Physics/BallState.hpp"

#include <string>
#include <string_view>

namespace BaseballSimulatorVR::UI
{
	class DesktopHudViewModel final
	{
	  public:
		struct WindowTitleInput final
		{
			Physics::BallState ballState{};
			std::wstring_view xrRuntimeName{};
			std::wstring_view xrSessionName{};
			std::wstring_view renderBackendName{};
			std::wstring_view operationModeName{};
			std::wstring_view fxaaPresetName{};
			std::wstring_view presetName{};
			std::wstring_view integratorName{};
			double pitchSpeedMetersPerSecond{ 0.0 };
			double spinRpm{ 0.0 };
			double windForwardVelocity{ 0.0 };
			double windCrossVelocity{ 0.0 };
			bool isHeightWindEnabled{ false };
			bool isSeamPerturbationEnabled{ false };
			double windShearFactor{ 0.0 };
			double seamPerturbationAmplitude{ 0.0 };
			double playbackSpeedPercent{ 0.0 };
			double fxaaEdgeThreshold{ 0.0 };
			double fxaaEdgeThresholdMin{ 0.0 };
			double fxaaBlendFactor{ 0.0 };
			double trailOpacityScale{ 1.0 };
			std::wstring_view latestResultLabel{};
			double latestResultScore{ 0.0 };
			bool isSessionLogAvailable{ false };
			double framesPerSecond{ 0.0 };
			double averageFrameTimeMilliseconds{ 0.0 };
		};

		[[nodiscard]] std::wstring BuildWindowTitle(const WindowTitleInput& input) const;
	};
} // namespace BaseballSimulatorVR::UI

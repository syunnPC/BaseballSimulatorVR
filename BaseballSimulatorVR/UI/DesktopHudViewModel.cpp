#include "DesktopHudViewModel.hpp"

#include <iomanip>
#include <sstream>

namespace BaseballSimulatorVR::UI
{
	namespace
	{
		[[nodiscard]] std::wstring_view ToOnOffLabel(const bool isEnabled) noexcept
		{
			return isEnabled ? L"On" : L"Off";
		}

		void AppendVectorSegment(
		    std::wostringstream* const stream,
		    const std::wstring_view label,
		    const Physics::Vector3& value)
		{
			if (stream == nullptr)
			{
				return;
			}
			(*stream) << L" | " << label << L"(" << value.x << L", " << value.y
			          << L", " << value.z << L")";
		}
	} // namespace

	std::wstring DesktopHudViewModel::BuildWindowTitle(
	    const WindowTitleInput& input) const
	{
		std::wostringstream stream{};
		stream << std::fixed << std::setprecision(2);
		stream << L"BaseballSimulatorVR | XR Session: " << input.xrSessionName;
		stream << L" | XR: " << input.xrRuntimeName;
		stream << L" | Render: " << input.renderBackendName;
		stream << L" | Mode: " << input.operationModeName;
		stream << L" | FXAAp: " << input.fxaaPresetName;
		stream << L" | Preset: " << input.presetName;
		stream << L" | Integrator: " << input.integratorName;
		stream << L" | Speed: " << input.pitchSpeedMetersPerSecond << L"m/s";
		stream << L" | Spin: " << input.spinRpm << L"rpm";
		stream << L" | WindV(F/C): " << input.windForwardVelocity << L"/" << input.windCrossVelocity;
		stream << L" | HeightWind: " << ToOnOffLabel(input.isHeightWindEnabled);
		stream << L" | Seam: " << ToOnOffLabel(input.isSeamPerturbationEnabled);
		stream << L" | Shear: " << input.windShearFactor;
		stream << L" | SeamAmp: " << input.seamPerturbationAmplitude;
		stream << L" | Playback: " << input.playbackSpeedPercent << L"%";
		stream << L" | FXAA: " << std::setprecision(3) << input.fxaaEdgeThreshold;
		stream << L"/" << input.fxaaEdgeThresholdMin;
		stream << L"/" << input.fxaaBlendFactor;
		stream << std::setprecision(2);
		stream << L" | TrailOpacity: " << input.trailOpacityScale;
		stream << L" | Result: " << input.latestResultLabel << L"(" << input.latestResultScore << L")";
		stream << L" | Log: " << ToOnOffLabel(input.isSessionLogAvailable);
		stream << L" | FPS: " << std::setprecision(1) << input.framesPerSecond;
		stream << L" | Avg: " << std::setprecision(2) << input.averageFrameTimeMilliseconds << L"ms";
		stream << std::setprecision(2);
		AppendVectorSegment(&stream, L"Pos", input.ballState.position);
		AppendVectorSegment(&stream, L"Vel", input.ballState.velocity);
		return stream.str();
	}
} // namespace BaseballSimulatorVR::UI

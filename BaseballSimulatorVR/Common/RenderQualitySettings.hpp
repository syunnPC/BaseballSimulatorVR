#pragma once

#include <algorithm>

namespace BaseballSimulatorVR::Common
{
	struct RenderQualitySettings final
	{
		float fxaaEdgeThreshold{ 0.125F };
		float fxaaEdgeThresholdMin{ 0.0312F };
		float fxaaBlendFactor{ 0.75F };
		float trailOpacityScale{ 1.0F };
	};

	[[nodiscard]] inline RenderQualitySettings NormalizeRenderQualitySettings(
	    const RenderQualitySettings& renderQualitySettings)
	{
		RenderQualitySettings normalized = renderQualitySettings;
		normalized.fxaaEdgeThreshold =
		    std::clamp(normalized.fxaaEdgeThreshold, 0.02F, 0.40F);
		normalized.fxaaEdgeThresholdMin = std::clamp(
		    normalized.fxaaEdgeThresholdMin,
		    0.001F,
		    normalized.fxaaEdgeThreshold);
		normalized.fxaaBlendFactor =
		    std::clamp(normalized.fxaaBlendFactor, 0.0F, 1.0F);
		normalized.trailOpacityScale =
		    std::clamp(normalized.trailOpacityScale, 0.50F, 1.50F);
		return normalized;
	}
} // namespace BaseballSimulatorVR::Common

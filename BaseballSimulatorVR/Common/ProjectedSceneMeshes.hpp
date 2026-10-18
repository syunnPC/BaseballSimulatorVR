#pragma once

#include "DebugWorldProjection.hpp"
#include "PitcherPresentation.hpp"

namespace BaseballSimulatorVR::Common
{
	template <typename TTriangle>
	void EmitProjectedPitcherMesh(const PitcherPresentationState& state,
	    const ViewProjectionState& view, LONG width, LONG height, TTriangle&& triangle)
	{
		if (!state.visible || !view.isValid || width <= 0 || height <= 0)
		{
			return;
		}
		static thread_local WorldMeshProjectionWorkspace workspace;
		workspace.Emit(GetPitcherWorldMesh(state),
		    PreparedWorldProjection(view, width, height, DebugWorldLayout::NearClipMeters,
		        DebugWorldLayout::FarClipMeters),
		    triangle);
	}
} // namespace BaseballSimulatorVR::Common

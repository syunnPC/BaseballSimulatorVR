#pragma once

namespace BaseballSimulatorVR::Common
{
	struct OverlayVisibilitySettings final
	{
		bool showPitchTrajectory{ true };
		bool showBattedTrajectory{ true };
		bool showStrikeZone{ true };
		bool showFloorMesh{ true };
	};
} // namespace BaseballSimulatorVR::Common

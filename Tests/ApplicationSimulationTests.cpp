#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Core/Application.hpp"
#include "../BaseballSimulatorVR/Common/DebugWorldProjection.hpp"
#include "../BaseballSimulatorVR/Common/PitcherPresentation.hpp"
#include "../BaseballSimulatorVR/Common/ZoneMaskUtilities.hpp"
#include "../BaseballSimulatorVR/Common/BatPoseUtilities.hpp"
#include "../BaseballSimulatorVR/Physics/BatGeometry.hpp"

#include <cmath>
#include <array>
#include <limits>
#include <memory>
#include <numbers>

namespace
{
	using namespace BaseballSimulatorVR;

	class TestXrSystem final : public Xr::IXrSystem
	{
	  public:
		Xr::HandPoseState leftHand{};
		Xr::HandPoseState rightHand{};
		Common::FloorEyeHeightSample floorSample{};
		Common::OverlayVisibilitySettings overlayVisibility{};
		bool Initialize(HWND) override { return true; }
		bool AttachGraphicsBinding(const Common::GraphicsBinding&) override { return true; }
		void SetBallState(const Physics::BallState&) override {}
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings& settings) override { overlayVisibility = settings; }
		void SetRenderQuality(const Common::RenderQualitySettings&) override {}
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>&) override {}
		void SetBatOverlayState(const std::optional<Common::BatOverlayState>&) override {}
		void SetFieldPresentationState(const Common::FieldPresentationState&) override {}
		void SetUiTelemetry(const Xr::UiTelemetry&) override {}
		void PollInput() override {}
		std::vector<Xr::UiCommand> ConsumePendingUiCommands() override { return {}; }
		void Shutdown() override {}
		std::wstring_view GetRuntimeName() const noexcept override { return L"test"; }
		std::wstring_view GetSessionName() const noexcept override { return L"test"; }
		Common::ViewProjectionState GetLeftEyeViewState() const noexcept override { return {}; }
		Common::FloorEyeHeightSample GetFloorEyeHeightSample() const noexcept override { return floorSample; }
		Xr::HandPoseState GetLeftHandPose() const noexcept override { return leftHand; }
		Xr::HandPoseState GetRightHandPose() const noexcept override { return rightHand; }
	};

	class TestRenderBackend final : public Render::IRenderBackend
	{
	  public:
		Common::OverlayVisibilitySettings overlayVisibility{};
		bool Initialize(HWND) override { return true; }
		void BeginFrame() override {}
		void RenderSimulation(const Physics::BallState&) override {}
		void EndFrame() override {}
		void Shutdown() override {}
		std::wstring_view GetBackendName() const noexcept override { return L"test"; }
		Common::GraphicsBinding GetGraphicsBinding() const noexcept override { return {}; }
		void SetFieldPresentationState(const Common::FieldPresentationState&) override {}
		void SetDesktopMirrorView(const Common::ViewProjectionState&) override {}
		void SetOverlayVisibility(const Common::OverlayVisibilitySettings& settings) override { overlayVisibility = settings; }
		void SetRenderQuality(const Common::RenderQualitySettings&) override {}
		void SetDuelZonePassMarker(const std::optional<Physics::Vector3>&) override {}
		void SetBatOverlayState(const std::optional<Common::BatOverlayState>&) override {}
	};

	struct RuntimeSettingsGuard final
	{
		double pitchDistance{ Common::GetPitchDistanceMeters() };
		Common::StrikeZoneRuntimeSettings zone{ Common::GetStrikeZoneRuntimeSettings() };
		Common::GroundRuntimeSettings ground{ Common::GetGroundRuntimeSettings() };
		~RuntimeSettingsGuard()
		{
			Common::SetPitchDistanceMeters(pitchDistance);
			Common::SetStrikeZoneRuntimeSettings(zone);
			Common::SetGroundRuntimeSettings(ground);
		}
	};

	bool Near(const double a, const double b) { return std::abs(a - b) < 1e-8; }
	bool SamePosition(const Physics::Vector3& a, const Physics::Vector3& b)
	{
		return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
	}
}

namespace BaseballSimulatorVR::Core
{
	struct ApplicationTestAccess final
	{
		static std::unique_ptr<Application> MakeApplication(TestXrSystem** xrOutput = nullptr)
		{
			auto xr = std::make_unique<TestXrSystem>();
			if (xrOutput != nullptr) *xrOutput = xr.get();
			return std::make_unique<Application>(std::move(xr),
			    std::make_unique<TestRenderBackend>(), Data::PresetRepository{});
		}

		static void SetUpTee(Application& app)
		{
			app.m_operationMode = Application::OperationMode::TeeBatting;
			app.m_environmentSettings.moundHeightOffsetMeters = 0.0;
			app.m_teeBallPosition = { 0.0, 1.0, app.m_environmentSettings.pitchDistanceMeters };
			app.m_lastFrameDeltaSeconds = 0.01;
			app.m_lastSimulationDeltaSeconds = 0.01;
			app.ResetPitchSimulation();
		}

		static void MoveBat(Application& app, TestXrSystem& xr, const double x, const double time)
		{
			xr.rightHand = { true, time, 1,
			    { x, 1.0, -2.1 + Physics::BatGripCenterRatio * app.m_environmentSettings.batLengthMeters },
			    { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
			app.RefreshBatPoseFromXrInput();
		}

		static Physics::BallState UpdateBall(Application& app)
		{
			auto ball = app.m_pitchSimulationService.GetBallState();
			app.UpdateModeSpecificBallState(&ball);
			return ball;
		}

		static void TestTeeCollisionAndPause()
		{
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			SetUpTee(*app);
			MoveBat(*app, *xr, -0.2, 1.0);
			(void)UpdateBall(*app);
			Tests::Check(!app->m_isTeeBallInFlight && app->m_hasPreviousTeeBallState,
			             "A resting tee ball retains history for the next swept bat test");
			MoveBat(*app, *xr, 0.2, 1.01);
			const auto hit = UpdateBall(*app);
			Tests::Check(app->m_isTeeBallInFlight && app->m_teeBattedMetrics.hasContact,
			             "Moving a tracked bat across the resting tee ball launches it");
			Tests::Check(hit.velocity.x > 1.0 && hit.position.x > 0.0,
			             "The application applies bat contact velocity and advances the outgoing ball");
			Tests::Check(Near(hit.flightTimeSeconds, 0.0068),
			    "A tee hit advances only the portion of its contact frame remaining after impact");
			const auto hitPresentation = app->BuildFieldPresentationState();
			Tests::Check(hitPresentation.ballFlightPhase == Common::BallFlightPhase::Batted &&
			    hitPresentation.battedContactWorld && hitPresentation.statistics.hasBattedMetrics &&
			    Near(hitPresentation.statistics.battedLaunchSpeedMetersPerSecond,
			        Physics::Length(app->m_teeBattedMetrics.launchVelocity)),
			    "A real tee collision publishes its contact point and stored launch speed for the current ball");
			Tests::Check(hitPresentation.statistics.hasBatContactPosition &&
			    Near(hitPresentation.statistics.batContactFraction,.60 / app->m_environmentSettings.batLengthMeters) &&
			    Near(hitPresentation.statistics.contactBatLengthMeters,app->m_environmentSettings.batLengthMeters),
			    "A real tee hit stores the handle-normalized impact position and the bat length used by the collision");
			const auto teeContact = hitPresentation.statistics;
			app->ResetPitchSimulation();
			app->m_environmentSettings.batLengthMeters = 1.1;
			const auto respawned = app->BuildFieldPresentationState().statistics;
			Tests::Check(respawned.hasBatContactPosition &&
			    Near(respawned.batContactFraction,teeContact.batContactFraction) &&
			    Near(respawned.contactBatLengthMeters,teeContact.contactBatLengthMeters),
			    "Tee respawn and later bat length changes retain the previous physical hit marker");

			app = MakeApplication(&xr);
			SetUpTee(*app);
			app->m_playbackPaused = true;
			app->m_lastSimulationDeltaSeconds = 0.0;
			MoveBat(*app, *xr, -0.2, 2.0);
			(void)UpdateBall(*app);
			MoveBat(*app, *xr, 0.2, 2.01);
			const auto pausedBall = UpdateBall(*app);
			Tests::Check(!app->m_isTeeBallInFlight && !app->m_teeBattedMetrics.hasContact &&
			    Physics::LengthSquared(pausedBall.velocity) == 0.0,
			    "A physical swing cannot launch a tee ball while playback is paused");
		}

		static void TestEnvironmentReset()
		{
			auto app = MakeApplication();
			SetUpTee(*app);
			app->m_isTeeBallInFlight = true;
			app->m_hasTeeBattedBallBounced = true;
			app->m_teePostBounceElapsedSeconds = 4.0;
			app->m_teeBattedMetrics.hasContact = true;
			app->m_hasPreviousTeeBallState = true;
			app->m_previousTeeBallState.position = { 20.0, 3.0, 4.0 };
			app->m_pitchSimulationService.SetBallState(app->m_previousTeeBallState);
			app->m_environmentSettings.moundHeightOffsetMeters = 0.30;
			app->ApplyEnvironmentSettingsChanges(L"test_reset", false);
			const auto resetBall = app->m_pitchSimulationService.GetBallState();
			Tests::Check(!app->m_isTeeBallInFlight && !app->m_hasTeeBattedBallBounced &&
			    !app->m_hasPreviousTeeBallState && app->m_teeBattedMetrics.hasContact &&
			    app->m_teePostBounceElapsedSeconds == 0.0,
			    "Changing the environment resets tee flight and collision history while retaining its last hit result");
			Tests::Check(SamePosition(resetBall.position, app->BuildTeeBallState().position) &&
			    Physics::LengthSquared(resetBall.velocity) == 0.0,
			    "Changing the environment restores the tee ball instead of a pitcher release ball");
			const auto nextBall = UpdateBall(*app);
			Tests::Check(SamePosition(nextBall.position, resetBall.position),
			             "The first frame after an environment reset cannot sweep from the old flying ball");
		}

		static void TestDuelReset()
		{
			auto app = MakeApplication();
			app->m_operationMode = Application::OperationMode::BattingDuel;
			app->m_activePitchParameters = app->m_presetRepository.LoadPitchParameters(Data::PitchPresetType::Fastball);
			app->ResetPitchSimulation();
			const auto flightStep = app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.4));
			app->AdvancePitchSimulation(flightStep);
			Tests::Check(app->m_pitchDeliveryController.HasReleased() &&
			    app->m_pitchSimulationService.GetBallState().position.z > 0.0,
			    "The duel test begins with a released moving pitch");
			app->m_duelZonePassMarker = Physics::Vector3{ 99.0, 99.0, 99.0 };
			app->m_duelPitchPredictedZonePassPoint = Physics::Vector3{ 99.0, 99.0, 99.0 };
			app->m_hasPreviousDuelBallState = true;
			app->m_hasDuelBallBeenBatted = true;
			app->m_isWaitingForNextDuelPitch = true;
			app->m_duelPitchCooldownRemainingSeconds = 2.0;
			app->m_environmentSettings.pitchDistanceMeters = 17.0;
			app->ApplyEnvironmentSettingsChanges(L"test_duel_reset", false);
			const auto state = app->BuildFieldPresentationState();
			const auto resetBall = app->m_pitchSimulationService.GetBallState();
			Tests::Check(!app->m_pitchDeliveryController.HasReleased() &&
			    Near(state.pitcher.secondsFromRelease, -Simulation::PitchDeliveryController::WindupSeconds) &&
			    !state.ballVisible && Near(resetBall.flightTimeSeconds, 0.0) && Near(resetBall.position.z, 0.0),
			    "Resetting a released duel pitch synchronizes hidden ball and pitcher windup");
			Tests::Check(!app->m_isWaitingForNextDuelPitch && !app->m_hasPreviousDuelBallState &&
			    !app->m_hasDuelBallBeenBatted && app->m_duelPitchCooldownRemainingSeconds == 0.0 &&
			    !app->m_duelZonePassMarker,
			    "Resetting a duel pitch discards previous flight, cooldown and result markers");
			Tests::Check(app->m_duelPitchPredictedZonePassPoint &&
			    Near(app->m_duelPitchPredictedZonePassPoint->z, 17.0) &&
			    std::abs(app->m_duelPitchPredictedZonePassPoint->x) < 99.0,
			    "Duel reset recomputes the preview at the new plate distance");
		}

		static void TestGroundOffset()
		{
			auto app = MakeApplication();
			SetUpTee(*app);
			app->m_environmentSettings.moundHeightOffsetMeters = 0.18;
			app->m_isTeeBallInFlight = true;
			app->m_teeBattedMetrics.hasContact = true;
			app->m_teeBattedMetrics.contactPosition = { 0.0, 1.0, 18.44 };
			app->m_hasPreviousTeeBallState = true;
			app->m_previousTeeBallState.position = { 0.0, 0.2 - 0.18, 10.0 };
			Physics::BallState falling;
			falling.position = { 0.0, 0.027 - 0.18, 9.9 };
			falling.velocity = { 0.0, -2.0, -5.0 };
			falling.flightTimeSeconds = 2.0;
			app->m_pitchSimulationService.SetBallState(falling);
			const auto bounced = UpdateBall(*app);
			Tests::Check(Near(bounced.position.y + 0.18, 0.037) && bounced.velocity.y > 0.0,
			             "With a mound offset the ball bounces at physical ground plus ball radius");
			Tests::Check(app->m_teeBattedMetrics.hasFirstGroundContact && app->m_hasTeeBattedBallBounced,
			             "Ground response and first-ground-contact statistics use the same offset plane");
			Tests::Check(SamePosition(bounced.position, app->m_pitchSimulationService.GetBallState().position),
			             "Ground response writes its corrected position back into the simulation");
		}

		static void TestGroundCarryInterpolationAndFirstBounce()
		{
			auto app = MakeApplication();
			app->m_operationMode = Application::OperationMode::BattingDuel;
			app->m_environmentSettings.moundHeightOffsetMeters = 0.0;
			app->m_environmentSettings.groundFanRadiusMeters = 100.0;
			app->ResetPitchSimulation();
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
			app->m_lastFrameDeltaSeconds = 0.25;
			app->m_lastSimulationDeltaSeconds = 0.5;
			app->m_hasDuelBallBeenBatted = true;
			app->m_duelBattedMetrics.hasContact = true;
			app->m_duelBattedMetrics.contactPosition = { 0.0, 1.0, 18.44 };
			app->m_hasPreviousDuelBallState = true;
			app->m_previousDuelBallState.position = { 0.0, 0.137, 18.44 - 100.0 };
			app->m_previousDuelBallState.flightTimeSeconds = 4.0;
			Physics::BallState falling{};
			falling.position = { 0.0, -0.063, 18.44 - 120.0 };
			falling.velocity = { 0.0, -2.0, -40.0 };
			falling.flightTimeSeconds = 4.5;
			app->m_pitchSimulationService.SetBallState(falling);
			(void)UpdateBall(*app);
			Tests::Check(app->m_duelBattedMetrics.hasFirstGroundContact &&
			    Near(app->m_duelBattedMetrics.firstGroundDistanceMeters, 110.0),
			    "Carry beyond the ground fan interpolates the first landing rather than the coarse frame endpoint");
			Tests::Check(app->m_isWaitingForNextDuelPitch,
			    "An out-of-field landing still starts the duel interval after measuring carry");
			(void)UpdateBall(*app);
			Tests::Check(Near(app->m_duelBattedMetrics.firstGroundDistanceMeters, 110.0),
			    "The first landing distance remains frozen throughout the next-pitch interval");

			TestXrSystem* xr = nullptr;
			app = MakeApplication(&xr);
			SetUpTee(*app);
			app->m_teeBallPosition.y = 0.4;
			app->m_teeBallPosition.z -= 1.0;
			app->ResetPitchSimulation();
			app->m_lastFrameDeltaSeconds = 0.05;
			app->m_lastSimulationDeltaSeconds = 0.05;
			xr->rightHand = { true, 1.0, 1, { -0.2, 0.6, -3.1 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
			app->RefreshBatPoseFromXrInput();
			(void)UpdateBall(*app);
			xr->rightHand = { true, 1.05, 1, { 0.6, -0.2, -3.1 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
			app->RefreshBatPoseFromXrInput();
			const auto bounced = UpdateBall(*app);
			Tests::Check(app->m_teeBattedMetrics.hasContact &&
			    app->m_teeBattedMetrics.hasFirstGroundContact && app->m_hasTeeBattedBallBounced,
			    "A downward tee hit records its first landing even when impact and bounce share one frame");
			Tests::Check(bounced.velocity.y > 0.0 && Near(bounced.position.y, 0.037),
			    "Same-frame tee landing also applies the physical ground response");
			const double firstCarry = app->m_teeBattedMetrics.firstGroundDistanceMeters;
			Tests::Check(firstCarry > 0.0 && firstCarry < 2.0,
			    "A same-frame low grounder reports its short first carry");
			Tests::Check(app->m_teeBattedMetrics.maxHeightMeters >=
			    app->m_teeBattedMetrics.contactPosition.y,
			    "A same-frame downward hit includes contact height in its recorded maximum height");
			app->m_previousTeeBallState.position = { 0.0, 0.137, 8.0 };
			falling.position = { 0.0, -0.063, 7.0 };
			app->m_pitchSimulationService.SetBallState(falling);
			(void)UpdateBall(*app);
			Tests::Check(Near(app->m_teeBattedMetrics.firstGroundDistanceMeters, firstCarry),
			    "A later bounce cannot replace the short grounder's first carry");
		}

		static void TestDuelBoardLifecycle()
		{
			auto app = MakeApplication();
			app->m_operationMode = Application::OperationMode::BattingDuel;
			app->m_activePresetType = Data::PitchPresetType::Custom;
			app->m_activePitchParameters.initialVelocity = { 1.0, 2.0, 40.0 };
			app->m_activePitchParameters.spinRpm = 2450.0;
			app->m_activePitchParameters.releaseHeightMeters = 1.98;
			app->m_pitchProfiles = { { L"当該フォーシーム", app->m_activePitchParameters },
			    { L"次のスプリット", app->m_activePitchParameters } };
			app->m_activePitchProfileIndex = 0;
			app->m_environmentSettings.moundHeightOffsetMeters = 0.18;
			app->m_duelPitchPreviewEnabled = false;
			app->ResetPitchSimulation();
			const auto hidden = app->BuildFieldPresentationState();
			Tests::Check(!hidden.statistics.pitchMetricsVisible && hidden.statistics.pitchName.empty() &&
			    hidden.statistics.pitchSpeedMetersPerSecond == 0.0 && hidden.statistics.pitchSpinRpm == 0.0 &&
			    !hidden.statistics.hasBattedMetrics && !hidden.ballVisible &&
			    hidden.ballFlightPhase == Common::BallFlightPhase::Stationary,
			    "Preview-off windup exposes neither the upcoming pitch values nor previous batted results");
			app->m_duelPitchPreviewEnabled = true;
			const auto preview = app->BuildFieldPresentationState();
			Tests::Check(preview.statistics.pitchMetricsVisible &&
			    preview.statistics.pitchName == L"当該フォーシーム" &&
			    Near(preview.statistics.pitchSpeedMetersPerSecond, std::sqrt(1605.0)) &&
			    preview.statistics.pitchSpinRpm == 2450.0 && !preview.ballVisible,
			    "Explicit duel preview reveals upcoming pitch metrics while the held ball stays hidden");
			app->m_duelPitchPreviewEnabled = false;
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.19));
			Tests::Check(!app->BuildFieldPresentationState().statistics.pitchMetricsVisible,
			    "Preview-off metrics remain hidden immediately before release");
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(0.01));
			const auto released = app->BuildFieldPresentationState();
			Tests::Check(released.ballVisible && !released.statistics.pitchMetricsVisible &&
			    released.ballFlightPhase == Common::BallFlightPhase::Pitch &&
			    released.statistics.pitchName.empty() && app->BuildXrUiTelemetry().pitchInformationVisible,
			    "Release reveals the actual ball and handheld information while the unresolved board stays concealed");
			app->m_pitchFlightAnalysis.ObservePosition({0.,1.,app->m_environmentSettings.pitchDistanceMeters +
			    Common::HomePlateDepthMeters + app->m_environmentSettings.ballProperties.radiusMeters});
			const auto resolved = app->BuildFieldPresentationState();
			Tests::Check(resolved.statistics.pitchMetricsVisible && resolved.statistics.pitchName == L"当該フォーシーム",
			    "A completed pitch publishes its frozen snapshot after clearing the whole plate");

			app->m_activePitchProfileIndex = 1;
			app->m_activePitchParameters.initialVelocity = { 0.0, 0.0, 20.0 };
			app->m_activePitchParameters.spinRpm = 500.0;
			app->m_activePitchParameters.releaseHeightMeters = 3.0;
			const auto frozen = app->BuildFieldPresentationState();
			Tests::Check(frozen.statistics.pitchName == resolved.statistics.pitchName &&
			    Near(frozen.statistics.pitchSpeedMetersPerSecond, resolved.statistics.pitchSpeedMetersPerSecond) &&
			    frozen.statistics.pitchSpinRpm == resolved.statistics.pitchSpinRpm &&
			    Near(frozen.pitcher.releasePositionWorld.y, 1.98 + 0.18),
			    "Changing the selected preset cannot mix its name or parameters into the delivered pitch snapshot");

			app->m_hasDuelBallBeenBatted = true;
			app->m_duelBattedMetrics.hasContact = true;
			app->m_duelBattedMetrics.contactPosition = { 0.2, 1.5, app->m_environmentSettings.pitchDistanceMeters };
			app->m_duelBattedMetrics.launchVelocity = { 30.0, 0.0, -40.0 };
			Physics::BallState slowerBall{};
			slowerBall.velocity = { 3.0, -1.0, -5.0 };
			app->m_pitchSimulationService.SetBallState(slowerBall);
			const auto batted = app->BuildFieldPresentationState();
			Tests::Check(batted.statistics.hasBattedMetrics &&
			    Near(batted.statistics.battedLaunchSpeedMetersPerSecond, 50.0) &&
			    batted.ballFlightPhase == Common::BallFlightPhase::Batted && batted.battedContactWorld &&
			    Near(batted.battedContactWorld->x, 0.2) && Near(batted.battedContactWorld->y, 1.68) &&
			    Near(batted.battedContactWorld->z, -Common::DebugWorldLayout::BatterToPlateDistanceMeters),
			    "Current contact displays its launch speed independently of later ball deceleration and reports world contact coordinates");
			app->m_isWaitingForNextDuelPitch = true;
			const auto interval = app->BuildFieldPresentationState();
			Tests::Check(interval.statistics.hasBattedMetrics && !interval.statistics.battedMetricsLive,
			    "A completed current-ball result remains visible during the inter-pitch interval");
			app->m_randomPitchProfileEnabled = false;
			app->StartRandomBattingDuelPitch(L"test_board_next_pitch");
			const auto nextPitch = app->BuildFieldPresentationState();
			Tests::Check(!nextPitch.statistics.hasBattedMetrics &&
			    nextPitch.statistics.battedLaunchSpeedMetersPerSecond == 0.0 && !nextPitch.battedContactWorld &&
			    !app->BuildXrUiTelemetry().hasBattedMetrics && !nextPitch.statistics.pitchMetricsVisible &&
			    nextPitch.statistics.pitchName.empty() && app->BuildXrUiTelemetry().pitchInformationVisible,
			    "Starting a fixed duel pitch clears the board while the handheld retains the selected pitch");
			Simulation::PitchSimulationService independentFlight;
			independentFlight.Reset(app->m_pitchSimulationService.GetPitchParameters());
			Physics::BallState previous = independentFlight.GetBallState();
			std::optional<Physics::Vector3> independentCrossing{};
			for (int step = 0; step < 8000; ++step)
			{
				independentFlight.Tick(std::chrono::duration<double>(0.001));
				const auto current = independentFlight.GetBallState();
				const double plate = app->m_environmentSettings.pitchDistanceMeters;
				if (previous.position.z < plate && current.position.z >= plate)
				{
					const double blend = (plate - previous.position.z) / (current.position.z - previous.position.z);
					independentCrossing = previous.position + (current.position - previous.position) * blend;
					break;
				}
				previous = current;
			}
			Tests::Check(independentCrossing && app->m_duelPitchPredictedZonePassPoint &&
			    Physics::Length(*independentCrossing - *app->m_duelPitchPredictedZonePassPoint) < 1e-8,
			    "The single reset-time prediction matches the actual next-pitch integration and plate crossing");
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
			Tests::Check(!app->BuildFieldPresentationState().statistics.pitchMetricsVisible,
			    "The next release keeps its result hidden until the plate decision");
			app->m_pitchFlightAnalysis.FinishContact(app->m_pitchSimulationService.GetBallState().position);
			Tests::Check(app->BuildFieldPresentationState().statistics.pitchName == L"次のスプリット",
			    "Resolving the next delivery publishes its own frozen pitch name");
			app->m_duelBattedMetrics.hasContact = true;
			app->m_duelBattedMetrics.launchVelocity = { 50.0, 0.0, 0.0 };
			app->ResetPitchSimulation();
			Tests::Check(!app->BuildFieldPresentationState().statistics.hasBattedMetrics &&
			    !app->BuildXrUiTelemetry().hasBattedMetrics,
			    "An explicit duel reset cannot retain a previous launch result");
		}

		static void TestFixedPitchVisibilityAndTrajectoryBoundary()
		{
			for (const bool randomPitch : { false, true })
			{
				for (const bool preview : { false, true })
				{
					auto app = MakeApplication();
					app->m_operationMode = Application::OperationMode::BattingDuel;
					app->m_randomPitchProfileEnabled = randomPitch;
					app->m_duelPitchPreviewEnabled = preview;
					app->m_activePresetType = Data::PitchPresetType::Custom;
					app->m_activePitchParameters.initialVelocity = { 0.0, 0.0, 40.0 };
					app->m_activePitchParameters.spinRpm = 2450.0;
					app->m_pitchProfiles = { { L"固定フォーシーム", app->m_activePitchParameters } };
					app->m_activePitchProfileIndex = 0;
					app->ResetPitchSimulation();
					const auto windup = app->BuildFieldPresentationState();
					const bool reveal = preview;
					Tests::Check(windup.statistics.pitchMetricsVisible == reveal &&
					    (reveal ? windup.statistics.pitchName == L"固定フォーシーム" &&
					        Near(windup.statistics.pitchSpeedMetersPerSecond, 40.0) &&
					        Near(windup.statistics.pitchSpinRpm, 2450.0)
					        : windup.statistics.pitchName.empty() &&
					            Near(windup.statistics.pitchSpeedMetersPerSecond, 0.0) &&
					            Near(windup.statistics.pitchSpinRpm, 0.0)),
					    "Every unresolved duel board stays hidden unless preview was explicitly enabled");
					Tests::Check(!windup.ballVisible && windup.trajectoryLifecycle.clearBatted &&
					    !windup.trajectoryLifecycle.retainBattedOnBallReset,
					    "A new duel windup explicitly clears the previous batted trajectory independently of preview");
					Tests::Check(app->BuildXrUiTelemetry().pitchInformationVisible == (!randomPitch || preview),
					    "The handheld preserves fixed-pitch and explicit-preview visibility during windup");
					(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
					Tests::Check(app->BuildFieldPresentationState().statistics.pitchMetricsVisible == preview &&
					    app->BuildXrUiTelemetry().pitchInformationVisible,
					    "Released pitch information appears on the handheld while the board still awaits the plate result");
					app->m_hasDuelBallBeenBatted = true;
					app->m_duelBattedMetrics.hasContact = true;
					app->m_isWaitingForNextDuelPitch = true;
					const auto interval = app->BuildFieldPresentationState();
					Tests::Check(!interval.ballVisible && !interval.trajectoryLifecycle.clearBatted &&
					    interval.ballFlightPhase == Common::BallFlightPhase::Batted,
					    "The inter-pitch interval hides the ball while preserving the just-completed hit trajectory");
				}
			}
		}

		static void TestActualDuelPlateDecisionAndResultRetention()
		{
			struct FlightCase { double frontX, releaseHeight; Common::PitchZoneCall call; bool clearAtFront; };
			const double halfWidth = Common::HomePlateWidthMeters * .5;
			const std::array<FlightCase,5> cases{{
				{0.,2.,Common::PitchZoneCall::Strike,false},
				{halfWidth + .0185,2.,Common::PitchZoneCall::Strike,false},
				{0.,2.65,Common::PitchZoneCall::Strike,true},
				{.20,2.65,Common::PitchZoneCall::Ball,true},
				{halfWidth + .07,2.,Common::PitchZoneCall::Ball,true}
			}};
			const auto checkConcealed = [](const Common::FieldStatisticsState& stats)
			{
				Tests::Check(!stats.pitchMetricsVisible && stats.pitchName.empty() &&
				    stats.pitchSpeedMetersPerSecond == 0. && stats.pitchSpinRpm == 0. &&
				    stats.zoneCall == Common::PitchZoneCall::Pending && stats.pitchTrace.empty() &&
				    !stats.hasPitchBreak && stats.pitchHorizontalBreakMeters == 0. && stats.pitchVerticalBreakMeters == 0.,
				    "An unresolved duel result exposes no name, speed, spin, call, break or flight trace");
			};
			for (const auto& test : cases)
			{
				auto app = MakeApplication();
				app->m_operationMode = Application::OperationMode::BattingDuel;
				app->m_activePresetType = Data::PitchPresetType::Custom;
				app->m_randomPitchProfileEnabled = false;
				app->m_duelPitchPreviewEnabled = false;
				app->m_environmentSettings.pitchDistanceMeters = 9.;
				app->m_environmentSettings.moundHeightOffsetMeters = 0.;
				app->m_environmentSettings.ballProperties.radiusMeters = .037;
				app->m_strikeZoneSettings = {.55,1.,.44};
				auto& parameters = app->m_activePitchParameters;
				parameters.initialVelocity = {test.frontX * 20. / 9.,0.,20.};
				parameters.releaseHeightMeters = test.releaseHeight;
				parameters.spinRpm = 2300.;
				parameters.usePhysicalAerodynamics = false;
				parameters.useCoefficientTable = false;
				parameters.dragCoefficient = 0.;
				parameters.liftCoefficient = 0.;
				parameters.enableSeamPerturbation = false;
				app->m_pitchProfiles = {{L"実投球の判定",parameters}};
				app->m_activePitchProfileIndex = 0;
				app->ResetPitchSimulation();
				checkConcealed(app->BuildFieldPresentationState().statistics);
				Tests::Check(app->BuildXrUiTelemetry().pitchInformationVisible,
				    "A fixed pitch remains readable on the handheld while the board conceals its unresolved result");
				app->m_duelPitchPreviewEnabled = true;
				const auto preview = app->BuildFieldPresentationState().statistics;
				Tests::Check(preview.pitchMetricsVisible && preview.pitchName == L"実投球の判定" &&
				    preview.hasPitchBreak && !preview.pitchTrace.empty() &&
				    preview.zoneCall == Common::PitchZoneCall::Pending,
				    "Explicit preview displays the predicted break and trace while the actual call remains pending");
				app->m_duelPitchPreviewEnabled = false;
				(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
				checkConcealed(app->BuildFieldPresentationState().statistics);
				app->m_lastSimulationDeltaSeconds = .001;
				const double endDepth = 9. + Common::HomePlateDepthMeters + .037;
				bool checkedFront = false, checkedRearMargin = false;
				for (int step = 0; step < 600 && !app->m_isWaitingForNextDuelPitch; ++step)
				{
					app->AdvancePitchSimulation(std::chrono::duration<double>(.001));
					auto ball = app->m_pitchSimulationService.GetBallState();
					app->UpdateModeSpecificBallState(&ball);
					const auto stats = app->BuildFieldPresentationState().statistics;
					if (ball.position.z < endDepth - 1e-9) checkConcealed(stats);
					else Tests::Check(stats.pitchMetricsVisible && stats.zoneCall == test.call,
					    "The actual call appears only after the entire ball clears the pentagonal plate rear");
					if (!checkedFront && ball.position.z >= 9.)
					{
						checkedFront = true;
						checkConcealed(stats);
						const auto crossing = app->m_pitchFlightAnalysis.Actual().frontCrossing;
						Tests::Check(crossing.has_value(),"The actual integrated flight records its front-plane crossing");
						if (test.clearAtFront)
							Tests::Check(!Common::SweptBallTouchesStrikeZone(*crossing,*crossing,.037,9.,.55,1.55),
							    "This integrated ball clears the front face, requiring the full plate volume for the final decision");
						if (test.frontX > halfWidth && test.call == Common::PitchZoneCall::Strike)
							Tests::Check(crossing->x > halfWidth,
							    "The grazing strike's center stays outside the plate while the spherical ball overlaps its side");
					}
					if (ball.position.z > 9. + Common::HomePlateDepthMeters && ball.position.z < endDepth)
					{
						checkedRearMargin = true;
						checkConcealed(stats);
					}
				}
				const auto result = app->BuildFieldPresentationState().statistics;
				Tests::Check(checkedFront && checkedRearMargin && app->m_isWaitingForNextDuelPitch &&
				    result.pitchMetricsVisible && result.zoneCall == test.call && result.pitchName == L"実投球の判定" &&
				    Near(result.pitchSpeedMetersPerSecond,Physics::Length(parameters.initialVelocity)) &&
				    Near(result.pitchSpinRpm,2300.) && result.hasPitchBreak && !result.pitchTrace.empty() &&
				    Near(result.pitchTrace.back().z,endDepth),
				    "Actual integrated plate results include stored pitch values, break and a trace ending at the full-ball boundary");
				const auto endPoint = result.pitchTrace.back();
				app->m_lastSimulationDeltaSeconds = .01;
				(void)UpdateBall(*app);
				const auto retained = app->BuildFieldPresentationState().statistics;
				Tests::Check(retained.zoneCall == result.zoneCall && retained.pitchName == result.pitchName &&
				    retained.pitchTrace.size() == result.pitchTrace.size() &&
				    SamePosition(retained.pitchTrace.back(),endPoint) && retained.hasPitchBreak &&
				    Near(retained.pitchVerticalBreakMeters,result.pitchVerticalBreakMeters) &&
				    Near(retained.pitchHorizontalBreakMeters,result.pitchHorizontalBreakMeters),
				    "The inter-pitch interval retains the completed decision, break and finite incoming flight trace");
				app->ResetPitchSimulation();
				checkConcealed(app->BuildFieldPresentationState().statistics);
				Tests::Check(app->m_pitchFlightAnalysis.Actual().call == Common::PitchZoneCall::Pending &&
				    !app->m_pitchFlightAnalysis.Actual().breakMeters &&
				    app->m_pitchFlightAnalysis.Actual().trace.size() == 1,
				    "Reset starts a fresh analysis without retaining the previous plate result or trace");
			}
		}

		static void TestLowFrameRateCurvedPitchAnalysis()
		{
			struct FrameResult { Simulation::PitchFlightReport report; bool frameChordMissed; };
			const auto run = [](const double frameSeconds) -> FrameResult
			{
				auto app = MakeApplication();
				app->m_operationMode = Application::OperationMode::BattingDuel;
				app->m_randomPitchProfileEnabled = false;
				app->m_duelPitchPreviewEnabled = false;
				app->m_environmentSettings.pitchDistanceMeters = 15.;
				app->m_environmentSettings.moundHeightOffsetMeters = 0.;
				app->m_environmentSettings.ballProperties.radiusMeters = .037;
				app->m_strikeZoneSettings = {.55,1.,.44};
				auto& parameters = app->m_activePitchParameters;
				parameters.initialVelocity = {0.,0.,40.};
				parameters.releaseHeightMeters = 1.8;
				parameters.spinAxis = {1.,0.,0.};
				parameters.spinRpm = 2300.;
				parameters.usePhysicalAerodynamics = false;
				parameters.useCoefficientTable = false;
				parameters.dragCoefficient = 0.;
				parameters.liftCoefficient = .03;
				parameters.enableSeamPerturbation = false;
				parameters.enableHeightDependentWind = false;
				app->ResetPitchSimulation();
				const auto front = app->m_pitchFlightAnalysis.Preview().frontCrossing;
				Tests::Check(front.has_value(),"The curved low-frame-rate pitch reaches the plate");
				parameters.releaseHeightMeters += .53 - front->y;
				app->ResetPitchSimulation();
				(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
				bool frameChordMissed = false;
				double accumulator = 0.;
				for (int frame = 0; frame < 100 && !app->m_pitchFlightAnalysis.IsResolved(); ++frame)
				{
					const auto previous = app->m_pitchSimulationService.GetBallState();
					accumulator += frameSeconds;
					while (accumulator >= .001)
					{
						app->AdvancePitchSimulation(std::chrono::duration<double>(.001));
						accumulator -= .001;
					}
					auto current = app->m_pitchSimulationService.GetBallState();
					if (previous.position.z < 15. && current.position.z >= 15.)
						frameChordMissed = !Common::SweptBallTouchesStrikeZone(previous.position,current.position,
						    .037,15.,.55,1.55);
					app->m_lastSimulationDeltaSeconds = frameSeconds;
					app->UpdateModeSpecificBallState(&current);
					Tests::Check(app->m_pendingPitchSamples.empty(),
					    "Every presentation frame commits and clears its buffered one-millisecond physics samples");
				}
				const auto report = app->m_pitchFlightAnalysis.Actual();
				const auto board = app->BuildFieldPresentationState().statistics;
				Tests::Check(report.call == Common::PitchZoneCall::Strike && report.frontCrossing && report.breakMeters &&
				    report.frontCrossing->y < .55 && report.frontCrossing->y > .55 - .037 &&
				    std::abs(report.breakMeters->y) > .001 && board.pitchMetricsVisible && board.hasPitchBreak,
				    "The actual curved sphere-grazing strike retains its measured front crossing and nonzero total bending");
				return {report,frameChordMissed};
			};
			const auto smooth = run(1. / 90.);
			const auto slow = run(1. / 4.);
			Tests::Check(slow.frameChordMissed,
			    "A single 250-millisecond presentation chord misses the curved strike and cannot classify this fixture correctly");
			Tests::Check(smooth.report.call == slow.report.call &&
			    SamePosition(*smooth.report.frontCrossing,*slow.report.frontCrossing) &&
			    SamePosition(*smooth.report.breakMeters,*slow.report.breakMeters) &&
			    SamePosition(smooth.report.trace.back(),slow.report.trace.back()),
			    "Ninety and four presentation frames per second produce the same actual call, front crossing, break and clipped trace endpoint");
		}

		static void TestTeeBoardResetLifecycle()
		{
			Tests::TempDirectory output;
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			SetUpTee(*app);
			const auto seedContact = [&]
			{
				app->m_isTeeBallInFlight = true;
				app->m_teeBattedMetrics.hasContact = true;
				app->m_teeBattedMetrics.hasFirstGroundContact = true;
				app->m_teeBattedMetrics.launchVelocity = { 18.0, 24.0, 0.0 };
				app->m_teeBattedMetrics.firstGroundDistanceMeters = 77.0;
				app->m_teeBattedMetrics.maxHeightMeters = 1.05;
				app->m_battedContactSequence = 11;
			};
			const auto checkRetainedResult = [&]
			{
				const auto presentation = app->BuildFieldPresentationState();
				const auto telemetry = app->BuildXrUiTelemetry();
				Tests::Check(presentation.statistics.hasBattedMetrics &&
				    Near(presentation.statistics.battedLaunchSpeedMetersPerSecond, 30.0) &&
				    Near(presentation.statistics.battedDistanceMeters, 77.0) &&
				    Near(presentation.statistics.battedMaxHeightMeters, 1.05) &&
				    !presentation.statistics.battedMetricsLive &&
				    !presentation.battedContactWorld &&
				    presentation.ballFlightPhase == Common::BallFlightPhase::Stationary &&
				    presentation.trajectoryLifecycle.retainBattedOnBallReset &&
				    presentation.trajectoryLifecycle.battedContactSequence == 11 &&
				    telemetry.hasBattedMetrics && !telemetry.battedMetricsLive &&
				    Near(telemetry.battedFirstGroundDistanceMeters, 77.0),
				    "A fresh tee ball preserves the last result without treating it as the current stationary ball's flight");
			};
			seedContact();
			Tests::Check(Near(app->BuildFieldPresentationState().statistics.battedLaunchSpeedMetersPerSecond, 30.0),
			    "The active tee ball shows its own stored launch speed");
			app->ResetPitchSimulation();
			checkRetainedResult();
			seedContact();
			app->AdjustTeeBallPosition(0.05, 0.5, 0.0);
			checkRetainedResult();
			(void)UpdateBall(*app);
			checkRetainedResult();
			seedContact();
			app->RefreshTeeBallSpawnFromZoneSelection(L"test_board_tee_spawn");
			checkRetainedResult();
			seedContact();
			app->m_hasTeeBattedBallBounced = true;
			app->m_teePostBounceElapsedSeconds = 100.0;
			Physics::BallState settled{};
			settled.position = { 0.0, 0.037, app->m_environmentSettings.pitchDistanceMeters };
			app->m_pitchSimulationService.SetBallState(settled);
			(void)UpdateBall(*app);
			checkRetainedResult();
			app->BeginStrikeZoneCalibration(L"test_tee_saved_result");
			checkRetainedResult();

			TestXrSystem* xr = nullptr;
			auto next = MakeApplication(&xr);
			SetUpTee(*next);
			next->m_teeBattedMetrics = app->m_teeBattedMetrics;
			next->m_battedContactSequence = 11;
			MoveBat(*next, *xr, -0.2, 5.0);
			(void)UpdateBall(*next);
			Tests::Check(Near(next->m_teeBattedMetrics.firstGroundDistanceMeters, 77.0),
			    "A new tracked bat pose cannot replace the saved result before contact");
			MoveBat(*next, *xr, 0.2, 5.01);
			(void)UpdateBall(*next);
			const auto newHit = next->BuildFieldPresentationState();
			Tests::Check(newHit.statistics.hasBattedMetrics && newHit.statistics.battedMetricsLive &&
			    newHit.ballFlightPhase == Common::BallFlightPhase::Batted && newHit.battedContactWorld &&
			    newHit.trajectoryLifecycle.battedContactSequence == 12 &&
			    !next->m_teeBattedMetrics.hasFirstGroundContact &&
			    Near(next->m_teeBattedMetrics.firstGroundDistanceMeters, 0.0) &&
			    !Near(newHit.statistics.battedLaunchSpeedMetersPerSecond, 30.0),
			    "The next physical tee contact atomically replaces result and trajectory identity with its new launch");
		}

		static void TestCalibrationIntegration()
		{
			Tests::TempDirectory output;
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			SetUpTee(*app);
			app->m_userDataDirectoryPath = output.path.wstring();
			app->m_environmentSettingsFilePath = (output.path / "environment.json").wstring();
			app->BeginStrikeZoneCalibration(L"test_calibration");
			using CalibrationStatus = Common::StrikeZoneCalibrationStatus;
			Tests::Check(app->BuildXrUiTelemetry().strikeZoneCalibration.status == CalibrationStatus::WaitingForFloor,
			    "Starting calibration publishes its waiting state to the hand UI");
			app->UpdateStrikeZoneCalibration();
			Tests::Check(app->BuildXrUiTelemetry().strikeZoneCalibration.status == CalibrationStatus::WaitingForFloor,
			    "The hand UI reports the missing physical floor reference");
			const double previousZoneHeight = app->m_strikeZoneSettings.heightMeters;
			xr->floorSample = { 1.2 * Common::EyeHeightToFullHeightFraction, 0.0, 1, true, true };
			app->UpdateStrikeZoneCalibration();
			Tests::Check(Near(app->m_strikeZoneSettings.heightMeters, previousZoneHeight),
			             "Starting asynchronous calibration preserves the previous zone until it completes");
			for (int frame = 1; frame <= 270; ++frame)
			{
				xr->floorSample.sampleTimeSeconds = static_cast<double>(frame) / 90.0;
				app->UpdateStrikeZoneCalibration();
				if (frame == 90)
				{
					const auto progress = app->BuildXrUiTelemetry().strikeZoneCalibration;
					Tests::Check(progress.status == CalibrationStatus::Steady &&
					    Near(progress.stableDurationSeconds, 1.0) && progress.fraction > .3 && progress.fraction < .4,
					    "Calibration telemetry reports stable-time progress during sampling");
				}
			}
			Tests::Check(app->BuildXrUiTelemetry().strikeZoneCalibration.status == CalibrationStatus::Complete &&
			    Near(app->BuildXrUiTelemetry().strikeZoneCalibration.fraction, 1.0),
			    "Completed calibration remains visible as complete in hand UI telemetry");
			Tests::Check(app->m_strikeZoneCalibrator.IsComplete() &&
			    Near(app->m_strikeZoneSettings.bottomMeters, 0.336) &&
			    Near(app->m_strikeZoneSettings.heightMeters, 0.504),
			    "Application calibration preserves a 1.2-meter player's proportional zone");
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), 0.336) &&
			    Near(Common::GetStrikeZoneTopMeters(), 0.84),
			    "Calibration updates the geometry used by rendering and zone decisions");
			Tests::Check(std::filesystem::exists(output.path / "PlaySettings.user.json") &&
			    !std::filesystem::exists(output.path / "environment.json"),
			    "Completed calibration persists the player's zone without creating or modifying an environment file");
			xr->floorSample.heightMeters = 1.9;
			xr->floorSample.sampleTimeSeconds = 4.0;
			app->UpdateStrikeZoneCalibration();
			Tests::Check(Near(app->m_strikeZoneSettings.heightMeters, 0.504),
			             "After calibration later HMD posture cannot continuously resize the zone");
			app->BeginStrikeZoneCalibration(L"test_explicit_recalibration");
			Tests::Check(!app->m_strikeZoneCalibrator.IsComplete() &&
			    Near(app->m_strikeZoneSettings.heightMeters, 0.504),
			    "Explicit recalibration clears sampling state while preserving the last usable zone");
		}

		static void TestIndependentTrajectoryCommandsAndPersistence()
		{
			using PitchDisplay = Common::PitchTrajectoryDisplayMode;
			using ZoneDisplay = Common::StrikeZoneDisplayMode;
			Tests::TempDirectory output;
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			app->m_userDataDirectoryPath = output.path.wstring();
			const auto settingsPath = output.path / "PlaySettings.user.json";
			std::wstring error;
			const auto* desktop = static_cast<TestRenderBackend*>(app->m_renderBackend.get());
			app->m_overlayVisibility.pitchTrajectoryDisplayMode = PitchDisplay::None;
			app->m_overlayVisibility.strikeZoneDisplayMode = ZoneDisplay::None;
			app->m_overlayVisibility.showBattedTrajectory = true;
			for (const auto expected : {PitchDisplay::Full, PitchDisplay::ZoneOnly, PitchDisplay::None})
			{
				app->HandleKeyDown('1');
				Tests::Check(app->m_overlayVisibility.pitchTrajectoryDisplayMode == expected &&
				    app->m_overlayVisibility.showBattedTrajectory &&
				    app->m_overlayVisibility.strikeZoneDisplayMode == ZoneDisplay::None &&
				    xr->overlayVisibility.pitchTrajectoryDisplayMode == expected &&
				    desktop->overlayVisibility.pitchTrajectoryDisplayMode == expected,
				    "Key 1 cycles all pitch display modes and updates both renderers independently");
				const auto saved = app->m_runtimeSettingsJsonStorage.LoadPlay(settingsPath.wstring(), error);
				Tests::Check(saved && saved->pitchTrajectoryDisplayMode == expected && saved->battedTrajectoryVisible,
				    "Every pitch display mode persists without changing batted visibility");
			}
			for (const auto expected : {ZoneDisplay::Plane, ZoneDisplay::Box, ZoneDisplay::None})
			{
				app->HandleKeyDown('2');
				Tests::Check(app->m_overlayVisibility.strikeZoneDisplayMode == expected &&
				    app->m_overlayVisibility.pitchTrajectoryDisplayMode == PitchDisplay::None &&
				    xr->overlayVisibility.strikeZoneDisplayMode == expected &&
				    desktop->overlayVisibility.strikeZoneDisplayMode == expected,
				    "Key 2 cycles all zone display modes independently in desktop and XR");
				const auto saved = app->m_runtimeSettingsJsonStorage.LoadPlay(settingsPath.wstring(), error);
				Tests::Check(saved && saved->strikeZoneDisplayMode == expected,
				    "Every zone display mode persists in PlaySettings");
			}
			for (const auto expected : {PitchDisplay::Full, PitchDisplay::ZoneOnly, PitchDisplay::None})
			{
				app->HandleXrUiCommand({Xr::UiCommandType::CyclePitchTrajectoryDisplayMode});
				Tests::Check(app->m_overlayVisibility.pitchTrajectoryDisplayMode == expected &&
				    app->m_overlayVisibility.showBattedTrajectory,
				    "The XR pitch button cycles all pitch display modes without changing batted visibility");
			}
			for (const auto expected : {ZoneDisplay::Plane, ZoneDisplay::Box, ZoneDisplay::None})
			{
				app->HandleXrUiCommand({Xr::UiCommandType::CycleStrikeZoneDisplayMode});
				Tests::Check(app->m_overlayVisibility.strikeZoneDisplayMode == expected,
				    "The XR zone button cycles all zone display modes");
			}
			app->HandleKeyDown('1');
			app->HandleKeyDown('1');
			app->HandleKeyDown('2');
			app->HandleKeyDown('4');
			for (const auto mode : {Application::OperationMode::BattingDuel,
			    Application::OperationMode::TeeBatting, Application::OperationMode::PitchTrajectory})
			{
				app->SetOperationMode(mode, L"test_independent_trails");
				Tests::Check(app->m_overlayVisibility.pitchTrajectoryDisplayMode == PitchDisplay::ZoneOnly &&
				    app->m_overlayVisibility.strikeZoneDisplayMode == ZoneDisplay::Plane &&
				    !app->m_overlayVisibility.showBattedTrajectory,
				    "Operation mode changes preserve both display modes and the independent batted preference");
			}
			const auto json = Tests::ReadFile(settingsPath);
			Tests::Check(json.find("\"pitchTrajectoryDisplayMode\"") != std::string::npos &&
			    json.find("\"strikeZoneDisplayMode\"") != std::string::npos &&
			    json.find("\"pitchTrajectoryVisible\"") == std::string::npos,
			    "PlaySettings saves the explicit display mode schema");
			auto reloaded = MakeApplication();
			reloaded->m_userDataDirectoryPath = output.path.wstring();
			Tests::Check(reloaded->LoadPlaySettingsFromJson() &&
			    reloaded->m_overlayVisibility.pitchTrajectoryDisplayMode == PitchDisplay::ZoneOnly &&
			    reloaded->m_overlayVisibility.strikeZoneDisplayMode == ZoneDisplay::Plane &&
			    !reloaded->m_overlayVisibility.showBattedTrajectory,
			    "Reloading restores the independent pitch, zone, and batted preferences");
			app->HandleXrUiCommand({Xr::UiCommandType::ToggleBattedTrajectoryVisibility});
			Tests::Check(app->m_overlayVisibility.showBattedTrajectory &&
			    app->m_overlayVisibility.pitchTrajectoryDisplayMode == PitchDisplay::ZoneOnly &&
			    xr->overlayVisibility.showBattedTrajectory && desktop->overlayVisibility.showBattedTrajectory,
			    "The XR batted button preserves pitch display mode in both renderers");
		}

		static void TestBatMaterialSelectionAndPersistence()
		{
			Tests::TempDirectory output;
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			app->m_userDataDirectoryPath = output.path.wstring();
			SetUpTee(*app);
			MoveBat(*app, *xr, -0.2, 1.0);
			Tests::Check(app->m_batOverlayState &&
			    app->m_batOverlayState->material == Physics::BatMaterial::Wood,
			    "The initial tracked bat uses the persisted wood material default");
			for (const auto material : { Physics::BatMaterial::Metal,
			    Physics::BatMaterial::Urethane, Physics::BatMaterial::Wood })
			{
				if (material == Physics::BatMaterial::Urethane) app->HandleKeyDown('9');
				else app->HandleXrUiCommand({ Xr::UiCommandType::CycleBatMaterial });
				Tests::Check(app->m_batMaterial == material && app->m_batOverlayState &&
				    app->m_batOverlayState->material == material &&
				    app->BuildXrUiTelemetry().batMaterial == material,
				    "XR and keyboard material commands cycle all three bats and update the existing presentation");
				auto restored = MakeApplication();
				restored->m_userDataDirectoryPath = output.path.wstring();
				Tests::Check(restored->LoadPlaySettingsFromJson() && restored->m_batMaterial == material,
				    "Every bat material preference survives an application settings reload");
			}

			std::array<double, 3> speeds{};
			for (std::size_t index = 0; index < speeds.size(); ++index)
			{
				app = MakeApplication(&xr);
				SetUpTee(*app);
				app->m_batMaterial = static_cast<Physics::BatMaterial>(index);
				MoveBat(*app, *xr, -0.2, 2.0);
				(void)UpdateBall(*app);
				MoveBat(*app, *xr, 0.2, 2.01);
				(void)UpdateBall(*app);
				Tests::Check(app->m_teeBattedMetrics.hasContact && app->m_batOverlayState &&
				    app->m_batOverlayState->material == app->m_batMaterial,
				    "A material-selected tracked bat produces a real swept tee collision");
				speeds[index] = Physics::Length(app->m_teeBattedMetrics.launchVelocity);
			}
			Tests::Check(speeds[0] < speeds[1] && speeds[1] < speeds[2],
			    "Identical real swings use the selected material's rebound response in the application");
		}

		static void TestBatGripAndFullLength()
		{
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			SetUpTee(*app);
			app->m_environmentSettings.batAngleOffsetDegrees = 25;
			app->m_environmentSettings.batYawOffsetDegrees = -15;
			double time = 1;
			for (const bool right : {false, true})
			for (const double length : {.40, .84, 1.20})
			{
				app->m_batUseRightHand = right;
				app->m_environmentSettings.batLengthMeters = length;
				auto& hand = right ? xr->rightHand : xr->leftHand;
				hand = {true, time += .01, 1, {.3, 1.2, -.5}, {.2, .4, -1}, {0, 1, 0}};
				const auto orientation = Common::BuildBatOrientation(hand.forward, hand.up, 25, -15);
				app->RefreshBatPoseFromXrInput();
				Tests::Check(orientation && app->m_batOverlayState.has_value(),
				    "Both controller hands produce a complete tilted bat at every supported length");
				const auto& bat = *app->m_batOverlayState;
				const auto knob = Common::ConvertSimulationPositionToWorld(bat.startSimulationPosition);
				const auto tip = Common::ConvertSimulationPositionToWorld(bat.endSimulationPosition);
				Tests::Check(Near(Physics::Length(tip - knob), length) &&
				    SamePosition(knob + orientation->axis * (length * Physics::BatGripCenterRatio), hand.position) &&
				    SamePosition(bat.upSimulationDirection, orientation->up),
				    "Configured bat length spans knob to tip and the controller remains inside the grip with matching roll");
				Tests::Check(Physics::BatGripStartRatio < Physics::BatGripCenterRatio &&
				    Physics::BatGripCenterRatio < Physics::BatGripEndRatio &&
				    Physics::BatRadiusAtFraction(Physics::BatGripCenterRatio, bat.radiusMeters) < .4 * bat.radiusMeters,
				    "The tracked hand lies in the thin grip rather than the taper or barrel");
			}
		}

		static void TestBatTrackingDiscontinuities()
		{
			for (int discontinuity = 0; discontinuity < 9; ++discontinuity)
			{
				TestXrSystem* xr = nullptr;
				auto app = MakeApplication(&xr);
				SetUpTee(*app);
				MoveBat(*app, *xr, -0.2, 1.0);
				(void)UpdateBall(*app);
				xr->rightHand.position.x = 0.2;
				xr->rightHand.sampleTimeSeconds = 1.01;
				switch (discontinuity)
				{
				case 0: xr->rightHand.isValid = false; break;
				case 1: xr->rightHand.referenceSpaceRevision = 2; break;
				case 2: xr->rightHand.sampleTimeSeconds = 1.0; break;
				case 3: xr->rightHand.sampleTimeSeconds = 0.99; break;
				case 4: xr->rightHand.sampleTimeSeconds = 1.2; break;
				case 5: xr->rightHand.position.x = std::numeric_limits<double>::quiet_NaN(); break;
				case 6: xr->rightHand.sampleTimeSeconds = std::numeric_limits<double>::quiet_NaN(); break;
				case 7: app->m_batUseRightHand = false; break;
				case 8: xr->rightHand.forward = { 0.0, 0.0, -1.0 }; break;
				}
				app->RefreshBatPoseFromXrInput();
				(void)UpdateBall(*app);
				Tests::Check(!app->m_isTeeBallInFlight && !app->m_teeBattedMetrics.hasContact,
				    "Tracking loss, coordinate/hand changes, stale time, gaps and nonfinite samples cannot invent a swing");
			}

			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			SetUpTee(*app);
			MoveBat(*app, *xr, -0.2, 2.0);
			(void)UpdateBall(*app);
			app->ResetPitchSimulation();
			Tests::Check(!app->m_batPoseState.isValid && !app->m_previousBatPoseState.isValid,
			    "Starting or resetting a pitch discards both sides of the previous bat-motion interval");
			MoveBat(*app, *xr, 0.2, 2.01);
			(void)UpdateBall(*app);
			Tests::Check(!app->m_isTeeBallInFlight, "The first post-reset sample establishes a pose without a collision sweep");
			app->TogglePlaybackPause(L"test");
			MoveBat(*app, *xr, -0.2, 2.02);
			(void)UpdateBall(*app);
			app->TogglePlaybackPause(L"test");
			MoveBat(*app, *xr, 0.2, 2.03);
			(void)UpdateBall(*app);
			Tests::Check(!app->m_isTeeBallInFlight,
			    "Resuming playback cannot convert a movement made during pause into an impact");
			MoveBat(*app, *xr, -0.2, 2.04);
			(void)UpdateBall(*app);
			Tests::Check(app->m_isTeeBallInFlight,
			    "Two fresh tracked poses after resuming still produce the real physical swing");
		}

		static void TestObliqueBatCollisionIntegration()
		{
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			app->m_operationMode = Application::OperationMode::BattingDuel;
			app->m_activePresetType = Data::PitchPresetType::Custom;
			app->m_duelPitchPreviewEnabled = false;
			app->m_environmentSettings.moundHeightOffsetMeters = 0.0;
			// Fixture: 0.85 m bat, 0.032 m radius, normal COR 0.5.
			app->m_environmentSettings.batLengthMeters = 0.85;
			app->m_environmentSettings.batRadiusMeters = 0.032;
			app->m_environmentSettings.ballProperties.restitutionAtReferenceSpeed = 0.5;
			app->m_activePitchParameters.initialVelocity = { 0.0, 0.0, 40.0 };
			app->m_activePitchParameters.spinAxis = { 1.0, 0.0, 0.0 };
			app->m_activePitchParameters.spinRpm = 2300.0;
			app->m_pitchProfiles = { { L"実際に投じた球種", app->m_activePitchParameters } };
			app->m_activePitchProfileIndex = 0;
			app->ResetPitchSimulation();
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
			Tests::Check(!app->BuildFieldPresentationState().statistics.pitchMetricsVisible,
			    "A released oblique pitch remains concealed until real bat contact resolves it");
			app->m_duelPitchPreviewEnabled = true;
			const auto before = app->BuildFieldPresentationState().statistics;
			app->m_duelPitchPreviewEnabled = false;
			const double depth = app->m_environmentSettings.pitchDistanceMeters;
			Physics::BallState ball;
			ball.position = { 0.0, 1.0, depth };
			ball.velocity = { 0.0, 0.0, 40.0 };
			ball.spinAxis = { 1.0, 0.0, 0.0 };
			ball.spinRpm = 2300.0;
			ball.flightTimeSeconds = 0.46;
			app->m_previousDuelBallState = ball;
			app->m_previousDuelBallState.position.z -= 0.4;
			app->m_previousDuelBallState.flightTimeSeconds -= 0.01;
			app->m_hasPreviousDuelBallState = true;
			const double ballWorldZ = -Common::DebugWorldLayout::BatterToPlateDistanceMeters;
			xr->rightHand = { true, 3.0, 1,
			    { -0.70 + Physics::BatGripCenterRatio * .85, 0.98, ballWorldZ + 0.2 },
			    { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };
			app->RefreshBatPoseFromXrInput();
			xr->rightHand.position.z -= 0.4;
			xr->rightHand.sampleTimeSeconds = 3.01;
			app->RefreshBatPoseFromXrInput();
			app->m_pitchSimulationService.SetBallState(app->m_previousDuelBallState);
			for (int sample = 0; sample < 11; ++sample)
				app->AdvancePitchSimulation(std::chrono::duration<double>(.001));
			app->AdvancePitchSimulation(std::chrono::duration<double>(.00025));
			ball = app->m_pitchSimulationService.GetBallState();
			const double incomingFrameEndTime = ball.flightTimeSeconds;
			const double incomingFrameStartTime = app->m_previousDuelBallState.flightTimeSeconds;
			Tests::Check(!app->m_pendingPitchSamples.empty() && app->m_pendingPitchSamples.back().position.z > depth,
			    "The contact frame buffers real incoming physics samples beyond the eventual impact and front plane");
			app->UpdateModeSpecificBallState(&ball);
			Tests::Check(app->m_hasDuelBallBeenBatted && app->m_duelBattedMetrics.hasContact,
			    "The production duel update reaches the real swept oblique bat collision solver");
			Tests::Check(ball.aerodynamicsModel == Physics::BallAerodynamicsModel::Physical &&
			    ball.spinRpm > 0.0 && !Near(ball.spinRpm, 2300.0) &&
			    Near(Physics::Length(ball.spinAxis), 1.0),
			    "Contact switches to measured batted-ball aerodynamics with the impulse-derived spin");
			const auto after = app->BuildFieldPresentationState().statistics;
			Tests::Check(after.pitchName == before.pitchName &&
			    Near(after.pitchSpeedMetersPerSecond, before.pitchSpeedMetersPerSecond) &&
			    Near(after.pitchSpinRpm, before.pitchSpinRpm) && after.hasBattedMetrics &&
			    !Near(after.battedLaunchSpeedMetersPerSecond, before.pitchSpeedMetersPerSecond),
			    "Real contact preserves delivered pitch name/speed/spin while publishing the new batted launch speed");
			Tests::Check(app->m_pendingPitchSamples.empty() &&
			    app->m_duelBattedMetrics.contactFlightTimeSeconds > incomingFrameStartTime &&
			    app->m_duelBattedMetrics.contactFlightTimeSeconds < incomingFrameEndTime,
			    "The real collision commits the contact-time prefix and discards all buffered post-contact samples");
			for (const auto& point : after.pitchTrace)
				Tests::Check(point.z <= app->m_duelBattedMetrics.contactPosition.z + 1e-8,
				    "Buffered incoming overshoot cannot place future points beyond the physical impact into the trace");
			Tests::Check(after.pitchMetricsVisible && after.zoneCall == Common::PitchZoneCall::Contact &&
			    !after.hasPitchBreak && !after.pitchTrace.empty() &&
			    SamePosition(after.pitchTrace.back(),app->m_duelBattedMetrics.contactPosition) &&
			    !SamePosition(after.pitchTrace.back(),ball.position),
			    "An early real hit reveals the incoming trace up to impact without inventing a future plate crossing or break");
			Tests::Check(after.hasBatContactPosition && after.batContactFraction >= 0. && after.batContactFraction <= 1. &&
			    Near(after.batContactFraction,.70 / .85) && Near(after.contactBatLengthMeters,.85),
			    "A real oblique hit publishes contact distance normalized from the true handle and the impact-time bat length");
			app->m_environmentSettings.batLengthMeters = 1.1;
			app->m_batPoseState = {};
			auto outgoing = ball;
			outgoing.position = {10.,3.,depth - 10.};
			app->m_pitchSimulationService.SetBallState(outgoing);
			app->UpdateModeSpecificBallState(&outgoing);
			const auto later = app->BuildFieldPresentationState().statistics;
			Tests::Check(later.pitchTrace.size() == after.pitchTrace.size() &&
			    SamePosition(later.pitchTrace.back(),after.pitchTrace.back()) && !later.hasPitchBreak &&
			    later.zoneCall == Common::PitchZoneCall::Contact && later.hasBatContactPosition &&
			    Near(later.batContactFraction,after.batContactFraction) && Near(later.contactBatLengthMeters,.85),
			    "Outgoing flight and later bat adjustments cannot alter the incoming trace or stored impact position");
			app->m_isWaitingForNextDuelPitch = true;
			const auto interval = app->BuildFieldPresentationState().statistics;
			Tests::Check(interval.hasBatContactPosition && Near(interval.batContactFraction,after.batContactFraction) &&
			    Near(interval.contactBatLengthMeters,.85) && interval.zoneCall == Common::PitchZoneCall::Contact &&
			    interval.pitchTrace.size() == after.pitchTrace.size(),
			    "The duel interval retains the actual bat contact marker with the completed incoming flight");
			const auto& frozen = app->m_pitchSimulationService.GetPitchParameters();
			Tests::Check(Near(Physics::Length(frozen.initialVelocity), 40.0) && Near(frozen.spinRpm, 2300.0),
			    "Writing the collision state cannot overwrite the pitch's frozen parameters");
			app->ResetPitchSimulation();
			Tests::Check(app->m_pitchSimulationService.GetBallState().aerodynamicsModel ==
			    Physics::BallAerodynamicsModel::PitchProfile,
			    "The next pitch resets the flight model together with velocity, spin and collision history");
			const auto reset = app->BuildFieldPresentationState().statistics;
			Tests::Check(!reset.hasBatContactPosition && !reset.hasBattedMetrics && !reset.pitchMetricsVisible &&
			    reset.pitchTrace.empty() && reset.zoneCall == Common::PitchZoneCall::Pending,
			    "A new delivery clears the old bat contact marker and incoming flight report together");
		}

		static void TestRequiredSettingsProvisioning()
		{
			Tests::TempDirectory output;
			const auto bundled = output.path / "bundled";
			const auto user = output.path / "user";
			Tests::Check(std::filesystem::create_directory(bundled) &&
			    std::filesystem::create_directory(user), "Create isolated bundled and user settings roots");
			const std::array<std::string_view, 33> names{
			    "PitchSettings.user.json", "PitchSettings.Yamamoto.json",
			    "PitchSettings.Difficulty.VeryEasy.RHP.json", "PitchSettings.Difficulty.VeryEasy.LHP.json",
			    "PitchSettings.Challenge1.RHP.json", "PitchSettings.Challenge1.LHP.json",
			    "PitchSettings.Challenge2.RHP.json", "PitchSettings.Challenge2.LHP.json",
			    "PitchSettings.Difficulty.Easy.RHP.json", "PitchSettings.Difficulty.Easy.LHP.json",
			    "PitchSettings.Difficulty.Normal.RHP.json", "PitchSettings.Difficulty.Normal.LHP.json",
			    "PitchSettings.Difficulty.Hard.RHP.json", "PitchSettings.Difficulty.Hard.LHP.json",
			    "PitchSettings.Difficulty.Expert.RHP.json", "PitchSettings.Difficulty.Expert.LHP.json",
			    "EnvironmentSettings.user.json", "PlaySettings.user.json",
			    "PitchSettings.Toss.Baseball.json", "PitchSettings.Toss.Softball.json",
			    "EnvironmentSettings.Toss.Baseball.json", "EnvironmentSettings.Toss.Softball.json",
			    "PitchSettings.SlowFastball.Baseball.RHP.json", "PitchSettings.SlowFastball.Baseball.LHP.json",
			    "PitchSettings.SlowFastball.Softball.RHP.json", "PitchSettings.SlowFastball.Softball.LHP.json",
			    "EnvironmentSettings.SlowFastball.Baseball.json", "EnvironmentSettings.SlowFastball.Softball.json",
			    "PitchSettings.Beginner.Softball.RHP.json", "PitchSettings.Beginner.Softball.LHP.json",
			    "PitchSettings.Beginner.BigBreak.Softball.RHP.json", "PitchSettings.Beginner.BigBreak.Softball.LHP.json",
			    "EnvironmentSettings.Beginner.Softball.json" };
			for (const auto name : names)
			{
				std::ofstream stream(bundled / name);
				stream << "bundled " << name;
			}
			auto app = MakeApplication();
			app->m_bundledContentDirectoryPath = bundled.wstring();
			app->m_userDataDirectoryPath = user.wstring();
			std::wstring error;
			Tests::Check(app->EnsureRequiredUserSettingsFiles(error),
			    "A new installation provisions all difficulty, toss and slow-fastball settings");
			for (const auto name : names)
				Tests::Check(Tests::ReadFile(user / name) == "bundled " + std::string(name),
				    "First-run provisioning copies each bundled file without changing its contents");
			{
				std::ofstream existing(user / names[0]);
				existing << "user modified";
			}
			{
				std::ofstream existing(user / names[3]);
				existing << "custom left-handed preset";
			}
			Tests::Check(std::filesystem::remove(user / names[8]), "Remove one preset to emulate an incomplete installation");
			Tests::Check(app->EnsureRequiredUserSettingsFiles(error),
			    "An existing installation fills missing difficulty presets");
			Tests::Check(Tests::ReadFile(user / names[0]) == "user modified" &&
			    Tests::ReadFile(user / names[3]) == "custom left-handed preset",
			    "Provisioning never overwrites existing user settings or modified difficulty files");
			Tests::Check(Tests::ReadFile(user / names[8]) == "bundled " + std::string(names[8]),
			    "Provisioning restores exactly the missing difficulty file from bundled content");
		}

		static void TestPitcherHandednessAndPresetTempoLifecycle()
		{
			Tests::TempDirectory output;
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			app->m_operationMode = Application::OperationMode::BattingDuel;
			Data::PitchRuntimeSettings settings;
			app->m_duelPitchPreviewEnabled = false;
			settings.pitcherThrowsLeftHanded = true;
			settings.pitchParameters.initialVelocity.z = 50.0;
			auto fast = settings.pitchParameters;
			fast.initialVelocity.z = Common::ReferencePitcherSpeedMetersPerSecond;
			auto slow = fast;
			slow.initialVelocity.z = 30.0;
			settings.pitchProfiles = { { L"Fast", fast }, { L"Slow", slow } };
			settings.activePitchProfileIndex = 1;
			settings.randomPitchProfileEnabled = false;
			const auto leftPath = output.path / "PitchSettings.Left.json";
			std::wstring error;
			Tests::Check(app->m_pitchPresetJsonStorage.SavePitchSettings(leftPath.wstring(), settings, error) &&
			    app->LoadUserPresetFromJsonFile(leftPath.wstring(), L"test_left_load"),
			    "Application loads a left-handed settings file with mixed pitch speeds");
			auto presentation = app->BuildFieldPresentationState();
			Tests::Check(presentation.pitcher.throwsLeftHanded && Near(presentation.pitcher.armSpeedScale, 1.0) &&
			    !presentation.statistics.pitchMetricsVisible && app->BuildXrUiTelemetry().pitchInformationVisible &&
			    Near(app->BuildXrUiTelemetry().pitchMetricsSpeedMetersPerSecond,30.0),
			    "The fastest profile sets tempo while fixed-pitch information stays on the handheld during windup");
			app->SelectPitchProfile(0, L"test_tempo_fast");
			Tests::Check(Near(app->BuildFieldPresentationState().pitcher.armSpeedScale, 1.0),
			    "Selecting the fast pitch retains the settings-file delivery tempo");
			app->SelectPitchProfile(1, L"test_tempo_slow");
			Tests::Check(Near(app->BuildFieldPresentationState().pitcher.armSpeedScale, 1.0),
			    "Selecting a change of speed does not reveal it through slower arm animation");
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
			presentation = app->BuildFieldPresentationState();
			const auto pose = Common::BuildPitcherPose(presentation.pitcher);
			const auto releaseBall = app->m_pitchSimulationService.GetBallState();
			const Physics::Vector3 releaseWorld{releaseBall.position.x,
			    releaseBall.position.y + app->m_environmentSettings.moundHeightOffsetMeters,
			    releaseBall.position.z - app->m_environmentSettings.pitchDistanceMeters -
			        Common::DebugWorldLayout::BatterToPlateDistanceMeters};
			Tests::Check(presentation.ballVisible && Near(presentation.pitcher.secondsFromRelease, 0.0) &&
			    SamePosition(pose.leftHand, releaseWorld),
			    "Left-handed presentation releases the ball at the delivery controller's exact release time");
			app->m_isWaitingForNextDuelPitch = true;
			Tests::Check(app->BuildFieldPresentationState().pitcher.throwsLeftHanded &&
			    Near(app->BuildFieldPresentationState().pitcher.armSpeedScale, 1.0),
			    "The interval preserves pitcher hand and preset tempo");
			app->ResetPitchSimulation();
			app->SaveUserPresetToJson();
			const auto saved = app->m_pitchPresetJsonStorage.LoadPitchSettings(leftPath.wstring(), error);
			Tests::Check(saved && saved->pitcherThrowsLeftHanded,
			    "Application saving preserves the root throwing-hand selection");
			app->SetOperationMode(Application::OperationMode::TeeBatting, L"test_left_tee");
			app->SetOperationMode(Application::OperationMode::BattingDuel, L"test_left_duel");
			Tests::Check(app->BuildFieldPresentationState().pitcher.throwsLeftHanded &&
			    Near(app->BuildFieldPresentationState().pitcher.armSpeedScale, 1.0),
			    "Mode changes and automatic pitch selection retain the file's hand and tempo");
			settings.pitcherThrowsLeftHanded = false;
			settings.pitchProfiles.clear();
			settings.pitchParameters.initialVelocity.z = 20.0;
			const auto rightPath = output.path / "PitchSettings.Right.json";
			Tests::Check(app->m_pitchPresetJsonStorage.SavePitchSettings(rightPath.wstring(), settings, error) &&
			    app->LoadUserPresetFromJsonFile(rightPath.wstring(), L"test_right_load"),
			    "Application switches from a left-handed file to a right-handed root-only file");
			presentation = app->BuildFieldPresentationState();
			Tests::Check(!presentation.pitcher.throwsLeftHanded &&
			    Near(presentation.pitcher.armSpeedScale, Common::PitcherArmSpeedScale(20.0)),
			    "A root-only file derives tempo from its own speed and replaces the previous throwing hand");
		}

		static void TestTossFileSwitchingAndBallProperties()
		{
			Tests::TempDirectory output;
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			std::wstring error;
			for (const int kind : {0, 1, 2})
			{
				const std::wstring name = kind == 0 ? L"Regular" : (kind == 1 ? L"Toss.Baseball" : L"Toss.Softball");
				Data::EnvironmentRuntimeSettings environment;
				environment.pitchDistanceMeters = kind == 0 ? 18.44 : 9.0;
				environment.moundHeightOffsetMeters = kind == 0 ? .18 : 0.0;
				if (kind == 2) environment.ballProperties = {.0485, .190};
				const std::wstring environmentName = L"EnvironmentSettings." + name + L".json";
				Tests::Check(app->m_runtimeSettingsJsonStorage.SaveEnvironment(
				    (output.path / environmentName).wstring(), environment, error), "Write file-switch environment fixture");
				Data::PitchRuntimeSettings settings;
				settings.environmentSettingsFile = environmentName;
				settings.randomPitchProfileEnabled = false;
				settings.activePitchProfileIndex = 0;
				if (kind != 0)
				{
					settings.pitchParameters.initialVelocity = {0, 8 * std::tan(35.0 * std::numbers::pi / 180.0), 8};
					settings.pitchParameters.releaseHeightMeters = 1.0;
					settings.pitchParameters.launchElevationDegrees = 35;
					settings.pitchParameters.underhandToss = true;
					settings.pitchParameters.usePhysicalAerodynamics = true;
					settings.pitchParameters.spinRpm = 0;
				}
				settings.pitchProfiles = {{name, settings.pitchParameters}};
				Tests::Check(app->m_pitchPresetJsonStorage.SavePitchSettings(
				    (output.path / (L"PitchSettings." + name + L".json")).wstring(), settings, error),
				    "Write file-switch pitch fixture");
			}
			Tests::Check(app->LoadUserPresetFromJsonFile((output.path / "PitchSettings.Regular.json").wstring()) &&
			    app->LoadEnvironmentSettingsFromJson(), "Load regular pitch and environment before switching");
			app->m_operationMode = Application::OperationMode::BattingDuel;
			app->m_duelPitchPreviewEnabled = false;
			for (const int kind : {1, 2})
			{
				app->SelectPitchSettingsFileByOffset(1, L"test_toss_switch");
				const double radius = kind == 1 ? .037 : .0485;
				const double mass = kind == 1 ? .145 : .190;
				const auto ball = app->m_pitchSimulationService.GetBallState();
				const auto field = app->BuildFieldPresentationState();
				Tests::Check(Near(app->m_environmentSettings.pitchDistanceMeters, 9.0) &&
				    Near(app->m_environmentSettings.moundHeightOffsetMeters, 0) &&
				    Near(app->m_activePitchParameters.initialVelocity.z, 8) &&
				    app->m_activePitchParameters.initialVelocity.y > 4.0 &&
				    ball.aerodynamicsModel == Physics::BallAerodynamicsModel::Physical &&
				    Near(ball.properties.radiusMeters, radius) && Near(ball.properties.massKilograms, mass),
				    "File switching applies the toss environment, low speed and selected ball before the next delivery");
				Tests::Check(field.pitcher.underhandToss && Near(field.pitcher.heldBallRadiusMeters, radius) &&
				    Near(field.pitcher.releasePositionWorld.y, 1) && !field.statistics.pitchMetricsVisible &&
				    app->BuildXrUiTelemetry().pitchInformationVisible && app->m_duelPitchPredictedZonePassPoint.has_value(),
				    "Toss presentation and targeted flight share release and ball radius while the fixed-pitch board awaits its result");
				const double cellWidth = Common::GetStrikeZoneHalfWidthMeters() * 2.0 / 3.0;
				const double cellHeight = (Common::GetStrikeZoneTopMeters() - Common::GetStrikeZoneBottomMeters()) / 3.0;
				const auto inCell = [&](const Physics::Vector3& point, int index)
				{
					const double left = -Common::GetStrikeZoneHalfWidthMeters() - cellWidth + (index % 5) * cellWidth;
					const double top = Common::GetStrikeZoneTopMeters() + cellHeight - (index / 5) * cellHeight;
					return point.x >= left - 1e-8 && point.x <= left + cellWidth + 1e-8 &&
					    point.y <= top + 1e-8 && point.y >= top - cellHeight - 1e-8;
				};
				for (int zone = 0; zone < 25; ++zone)
				{
					app->m_duelTargetZoneMask = 0;
					app->HandleXrUiCommand({Xr::UiCommandType::SetDuelTargetZone, zone});
					app->StartRandomBattingDuelPitch(L"test_toss_zone");
					Tests::Check(app->BuildXrUiTelemetry().duelTargetZoneMask == Common::BuildZoneBit(zone) &&
					    app->m_duelPitchPredictedZonePassPoint && inCell(*app->m_duelPitchPredictedZonePassPoint, zone),
					    "Both toss balls use the regular zone-selection command and target every one of the 25 cells");
					Simulation::PitchSimulationService flight;
					flight.Reset(app->m_activePitchParameters);
					auto current = flight.GetBallState();
					double apex = current.position.y;
					bool reached = false;
					for (int step = 0; step < 2000; ++step)
					{
						const auto previous = current;
						flight.Tick(std::chrono::duration<double>(.001));
						current = flight.GetBallState();
						apex = (std::max)(apex, current.position.y);
						if (current.position.z >= 9.0)
						{
							const double blend = (9.0 - previous.position.z) / (current.position.z - previous.position.z);
							const auto crossing = previous.position + (current.position - previous.position) * blend;
							Tests::Check(inCell(crossing, zone) && current.velocity.y < 0 &&
							    apex > (std::max)(1.0, crossing.y) + .3,
							    "Independent physical integration reaches the selected toss zone on the descending side of a lob");
							reached = true;
							break;
						}
					}
					Tests::Check(reached, "Every selected toss zone is reachable within two seconds");
				}
				app->m_duelTargetZoneMask = 0;
				for (int zone : {0, 24}) app->HandleXrUiCommand({Xr::UiCommandType::SetDuelTargetZone, zone});
				for (int attempt = 0; attempt < 8; ++attempt)
				{
					app->StartRandomBattingDuelPitch(L"test_toss_multiple_zones");
					const auto point = app->m_duelPitchPredictedZonePassPoint;
					Tests::Check(point && (inCell(*point, 0) || inCell(*point, 24)),
					    "Tosses randomize only within the selected zone subset");
				}
				app->HandleXrUiCommand({Xr::UiCommandType::ClearDuelTargetZoneBias});
				Tests::Check(app->BuildXrUiTelemetry().duelTargetZoneMask == 0,
				    "Clearing the toss selection restores the regular all-zone targeting mode");
				app->SetOperationMode(Application::OperationMode::PitchTrajectory, L"test_toss_trajectory_zone");
				app->HandleXrUiCommand({Xr::UiCommandType::SetDuelTargetZone, 12});
				Simulation::PitchSimulationService trajectory;
				trajectory.Reset(app->m_activePitchParameters);
				auto trajectoryBall = trajectory.GetBallState();
				bool crossed = false;
				for (int step = 0; step < 2000; ++step)
				{
					const auto before = trajectoryBall;
					trajectory.Tick(std::chrono::duration<double>(.001));
					trajectoryBall = trajectory.GetBallState();
					if (trajectoryBall.position.z >= 9)
					{
						const double blend = (9 - before.position.z) / (trajectoryBall.position.z - before.position.z);
						Tests::Check(inCell(before.position + (trajectoryBall.position - before.position) * blend, 12),
						    "Trajectory mode applies the same selected zone to both toss balls");
						crossed = true;
						break;
					}
				}
				Tests::Check(crossed, "Zone-targeted toss trajectory reaches the selected plane");
				app->HandleXrUiCommand({Xr::UiCommandType::ClearDuelTargetZoneBias});
				app->SetOperationMode(Application::OperationMode::BattingDuel, L"test_toss_duel_restore");
			}
			app->SetOperationMode(Application::OperationMode::TeeBatting, L"test_softball_tee");
			const auto tee = app->BuildTeeBallState();
			Tests::Check(Near(tee.properties.radiusMeters, .0485) && Near(tee.properties.massKilograms, .190),
			    "Tee placement retains the selected softball properties");
			auto grounded = tee;
			grounded.position = {0, .04, app->m_environmentSettings.pitchDistanceMeters - 1.0};
			grounded.velocity = {0, -1, 0};
			app->m_isTeeBallInFlight = true;
			app->m_hasPreviousTeeBallState = true;
			app->m_previousTeeBallState = grounded;
			app->m_previousTeeBallState.position.y = .2;
			app->UpdateModeSpecificBallState(&grounded);
			Tests::Check(Near(grounded.position.y, .0485) && grounded.velocity.y > 0,
			    "Softball ground collision uses its physical radius");
			app->m_operationMode = Application::OperationMode::PitchTrajectory;
			{
				std::ofstream invalid(output.path / "EnvironmentSettings.Regular.json");
				invalid << "{}";
			}
			const auto selectedName = app->m_activePitchSettingsFileName;
			app->SelectPitchSettingsFileByOffset(1, L"test_invalid_environment");
			Tests::Check(app->m_activePitchSettingsFileName == selectedName &&
			    Near(app->m_environmentSettings.pitchDistanceMeters, 9),
			    "A preset with an invalid environment cannot partially replace the active selection");
			Tests::Check(app->m_runtimeSettingsJsonStorage.SaveEnvironment(
			    (output.path / "EnvironmentSettings.Regular.json").wstring(), {}, error),
			    "Restore the regular environment fixture");
			app->SelectPitchSettingsFileByOffset(1, L"test_regular_restore");
			Tests::Check(Near(app->m_environmentSettings.pitchDistanceMeters, 18.44) &&
			    Near(app->m_pitchSimulationService.GetBallState().properties.radiusMeters, .037) &&
			    !app->BuildFieldPresentationState().pitcher.underhandToss,
			    "Switching back restores the regular environment, baseball and overhand form");
		}

		static Physics::Vector3 SimulateZoneCrossing(const Baseball::PitchParameters& parameters,
		    const double pitchDistanceMeters, const double moundHeightOffsetMeters)
		{
			Simulation::PitchSimulationService flight;
			flight.Reset(parameters);
			auto current = flight.GetBallState();
			bool touchedGround = false;
			for (int step = 0; step < 2000; ++step)
			{
				const auto previous = current;
				flight.Tick(std::chrono::duration<double>(.001));
				current = flight.GetBallState();
				touchedGround = touchedGround || current.position.y +
				    moundHeightOffsetMeters < current.properties.radiusMeters;
				if (current.position.z >= pitchDistanceMeters)
				{
					Tests::Check(!touchedGround, "A targeted pitch reaches its zone before striking the ground");
					const double fraction = (pitchDistanceMeters - previous.position.z) / (current.position.z - previous.position.z);
					return previous.position + (current.position - previous.position) * fraction;
				}
			}
			throw std::runtime_error("Targeted pitch did not reach home plate within two seconds");
		}

		static void TestSlowFastballFilesAndZoneTargeting(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory output;
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			for (const std::wstring_view kind : {L"Baseball", L"Softball"})
			{
				const std::wstring environment = L"EnvironmentSettings.SlowFastball." + std::wstring(kind) + L".json";
				Tests::Check(std::filesystem::copy_file(sourceRoot / environment, output.path / environment),
				    "Copy the shipped normal-distance slow-fastball environment into an isolated user directory");
				for (const std::wstring_view hand : {L"RHP", L"LHP"})
				{
					const std::wstring name = L"PitchSettings.SlowFastball." + std::wstring(kind) + L"." + std::wstring(hand) + L".json";
					Tests::Check(std::filesystem::copy_file(sourceRoot / name, output.path / name),
					    "Copy the shipped right- or left-handed slow-fastball preset for application testing");
				}
			}
			for (const std::wstring_view kind : {L"Baseball", L"Softball"})
			{
				const double radius = kind == L"Baseball" ? .037 : .0485;
				const double mass = kind == L"Baseball" ? .145 : .190;
				for (const std::wstring_view hand : {L"RHP", L"LHP"})
				{
					const std::wstring name = L"PitchSettings.SlowFastball." + std::wstring(kind) + L"." + std::wstring(hand) + L".json";
					Tests::Check(app->LoadUserPresetFromJsonFile((output.path / name).wstring()) &&
					    app->LoadEnvironmentSettingsFromJson() && app->m_pitchProfiles.size() == 4 &&
					    app->m_randomPitchProfileEnabled && app->m_pitcherThrowsLeftHanded == (hand == L"LHP") &&
					    Near(app->m_environmentSettings.pitchDistanceMeters, 18.44) &&
					    Near(app->m_environmentSettings.moundHeightOffsetMeters, .18) &&
					    !app->m_environmentSettings.enableMoundHeightOffset,
					    "Loading each shipped slow-fastball file applies its handedness, four-speed repertoire and normal environment");
					for (int profile = 0; profile < 4; ++profile)
					{
						app->m_operationMode = Application::OperationMode::PitchTrajectory;
						app->m_duelTargetZoneMask = 0;
						app->HandleXrUiCommand({Xr::UiCommandType::SelectPitchProfile, profile});
						const auto parameters = app->m_activePitchParameters;
						const auto ball = app->m_pitchSimulationService.GetBallState();
						const auto field = app->BuildFieldPresentationState();
						Tests::Check(app->m_activePitchProfileIndex == profile && !app->m_randomPitchProfileEnabled &&
						    Near(Physics::Length(parameters.initialVelocity) * 3.6, 70 + profile * 10) &&
						    Near(ball.properties.radiusMeters, radius) && Near(ball.properties.massKilograms, mass) &&
						    ball.aerodynamicsModel == Physics::BallAerodynamicsModel::Physical &&
						    field.pitcher.throwsLeftHanded == (hand == L"LHP") && !field.pitcher.underhandToss &&
						    Near(field.pitcher.releasePositionWorld.y, 1.98),
						    "XR profile selection releases the exact requested slow-fastball speed with the selected physical ball and normal overhand pitcher");
						const auto rawCrossing = SimulateZoneCrossing(parameters, 18.44, app->m_environmentSettings.moundHeightOffsetMeters);
						Tests::Check(Near(rawCrossing.x, 0) && std::abs(rawCrossing.y - 1.05) < 1e-6,
						    "Unselected-zone trajectory mode delivers every slow fastball through the zone centre");
						app->m_operationMode = Application::OperationMode::BattingDuel;
						const double cellWidth = Common::GetStrikeZoneHalfWidthMeters() * 2 / 3;
						const double cellHeight = (Common::GetStrikeZoneTopMeters() - Common::GetStrikeZoneBottomMeters()) / 3;
						for (int zone = 0; zone < 25; ++zone)
						{
							app->m_duelTargetZoneMask = Common::BuildZoneBit(zone);
							app->StartRandomBattingDuelPitch(L"test_slow_fastball_zone");
							const auto crossing = SimulateZoneCrossing(app->m_activePitchParameters, 18.44, app->m_environmentSettings.moundHeightOffsetMeters);
							const double worldCrossingY = crossing.y + app->m_environmentSettings.moundHeightOffsetMeters;
							const double left = -Common::GetStrikeZoneHalfWidthMeters() - cellWidth + (zone % 5) * cellWidth;
							const double top = Common::GetStrikeZoneTopMeters() + cellHeight - (zone / 5) * cellHeight;
							const std::string context = std::string(kind == L"Baseball" ? "Baseball" : "Softball") + "/" +
							    (hand == L"RHP" ? "RHP" : "LHP") + " speed=" + std::to_string(70 + profile * 10) +
							    " zone=" + std::to_string(zone) + " crossing=(" + std::to_string(crossing.x) + "," +
							    std::to_string(crossing.y) + ") worldY=" + std::to_string(worldCrossingY) +
							    " cellY=[" + std::to_string(top - cellHeight) + "," + std::to_string(top) + "]";
							Tests::Check(app->m_activePitchProfileIndex == profile &&
							    crossing.x >= left - 1e-5 && crossing.x <= left + cellWidth + 1e-5 &&
							    worldCrossingY >= top - cellHeight - 1e-5 && worldCrossingY <= top + 1e-5,
							    "Every slow-fastball profile reaches its selected normal-distance zone: " + context);
						}
					}
				}
			}
		}

		static void TestAdditionalTrainingPresets(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory output;
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			Tests::Check(std::filesystem::copy_file(sourceRoot / "EnvironmentSettings.user.json",
			    output.path / "EnvironmentSettings.user.json"), "Copy the standard environment for additional training presets");
			for (const auto kind : {L"Difficulty.VeryEasy", L"Challenge1", L"Challenge2"})
			for (const auto hand : {L"RHP", L"LHP"})
			{
				const bool lowRelease = std::wstring_view{kind} == L"Challenge1";
				const double releaseHeight = lowRelease ? .1 : 1.8;
				const std::wstring name = L"PitchSettings." + std::wstring{kind} + L"." + hand + L".json";
				Tests::Check(std::filesystem::copy_file(sourceRoot / name, output.path / name) &&
				    app->LoadUserPresetFromJsonFile((output.path / name).wstring()) && app->LoadEnvironmentSettingsFromJson(),
				    "The additional training preset loads through the application with its referenced environment");
				const double mound = app->m_environmentSettings.moundHeightOffsetMeters;
				app->m_operationMode = Application::OperationMode::BattingDuel;
				for (int profile = 0; profile < static_cast<int>(app->m_pitchProfiles.size()); ++profile)
				{
					app->HandleXrUiCommand({Xr::UiCommandType::SelectPitchProfile, profile});
					for (const int zone : {0, 4, 12, 20, 24})
					{
						if (!lowRelease && zone != 12) continue;
						app->m_duelTargetZoneMask = Common::BuildZoneBit(zone);
						app->StartRandomBattingDuelPitch(L"test_training_preset_zone");
						const auto& parameters = app->m_activePitchParameters;
						const auto presentation = app->BuildFieldPresentationState();
						Tests::Check(Near(parameters.releaseHeightMeters, releaseHeight) &&
						    Near(presentation.pitcher.releasePositionWorld.y, releaseHeight + mound) &&
						    presentation.pitcher.throwsLeftHanded == (std::wstring_view{hand} == L"LHP"),
						    "Profile selection and repeated deliveries preserve the low release and pitcher handedness");
						const auto crossing = SimulateZoneCrossing(parameters, app->m_environmentSettings.pitchDistanceMeters, mound);
						const double cellWidth = Common::GetStrikeZoneHalfWidthMeters() * 2 / 3;
						const double cellHeight = (Common::GetStrikeZoneTopMeters() - Common::GetStrikeZoneBottomMeters()) / 3;
						const double left = -Common::GetStrikeZoneHalfWidthMeters() - cellWidth + (zone % 5) * cellWidth;
						const double top = Common::GetStrikeZoneTopMeters() + cellHeight - (zone / 5) * cellHeight;
						Tests::Check(crossing.x >= left - 1e-5 && crossing.x <= left + cellWidth + 1e-5 &&
						    crossing.y + mound >= top - cellHeight - 1e-5 && crossing.y + mound <= top + 1e-5,
						    "Low-release pitches reach the corner and centre zones without striking the ground");
						if (std::wstring_view{kind} == L"Challenge2")
							Tests::Check(Near(parameters.initialVelocity.z * 3.6, 170),
							    "Targeting preserves the configured 170 km/h two-seam forward speed");
					}
				}
			}
		}

		static void TestBeginnerSoftballZoneTargeting(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory output;
			const std::wstring environment = L"EnvironmentSettings.Beginner.Softball.json";
			Tests::Check(std::filesystem::copy_file(sourceRoot / environment, output.path / environment),
			    "Copy the shipped beginner softball environment for application targeting tests");
			auto app = MakeApplication();
			app->m_userDataDirectoryPath = output.path.wstring();
			constexpr std::array<double, 6> speedsKmh{90, 75, 80, 85, 85, 80};
			for (const bool bigBreak : {false, true})
			{
				for (const bool leftHanded : {false, true})
				{
					const std::wstring name = std::wstring(L"PitchSettings.Beginner.") +
					    (bigBreak ? L"BigBreak." : L"") + L"Softball." + (leftHanded ? L"LHP.json" : L"RHP.json");
					Tests::Check(std::filesystem::copy_file(sourceRoot / name, output.path / name) &&
					    app->LoadUserPresetFromJsonFile((output.path / name).wstring()) && app->LoadEnvironmentSettingsFromJson() &&
					    app->m_pitchProfiles.size() == 6 && app->m_randomPitchProfileEnabled,
					    "Both handedness choices load all six beginner softball pitches for normal and large breaks");
					for (int profile = 0; profile < 6; ++profile)
					{
						app->m_operationMode = Application::OperationMode::PitchTrajectory;
						app->m_duelTargetZoneMask = 0;
						app->HandleXrUiCommand({Xr::UiCommandType::SelectPitchProfile, profile});
						const auto ball = app->m_pitchSimulationService.GetBallState();
						Tests::Check(Near(Physics::Length(ball.velocity) * 3.6, speedsKmh[profile]) &&
						    Near(ball.properties.radiusMeters, .0485) && Near(ball.properties.massKilograms, .190) &&
						    app->m_pitcherThrowsLeftHanded == leftHanded && !app->m_randomPitchProfileEnabled &&
						    Near(app->m_environmentSettings.pitchDistanceMeters, 18.44) &&
						    Near(app->m_environmentSettings.moundHeightOffsetMeters, .18),
						    "XR selection preserves each beginner pitch's release speed, softball properties, handedness and normal distance");
						app->m_operationMode = Application::OperationMode::BattingDuel;
						const double cellWidth = Common::GetStrikeZoneHalfWidthMeters() * 2 / 3;
						const double cellHeight = (Common::GetStrikeZoneTopMeters() - Common::GetStrikeZoneBottomMeters()) / 3;
						for (int zone = 0; zone < 25; ++zone)
						{
							app->m_duelTargetZoneMask = Common::BuildZoneBit(zone);
							app->StartRandomBattingDuelPitch(L"test_beginner_softball_zone");
							const auto crossing = SimulateZoneCrossing(app->m_activePitchParameters, 18.44,
							    app->m_environmentSettings.moundHeightOffsetMeters);
							const double worldY = crossing.y + app->m_environmentSettings.moundHeightOffsetMeters;
							const double left = -Common::GetStrikeZoneHalfWidthMeters() - cellWidth + (zone % 5) * cellWidth;
							const double top = Common::GetStrikeZoneTopMeters() + cellHeight - (zone / 5) * cellHeight;
							const std::string context = std::string(bigBreak ? "BigBreak" : "Normal") + "/" +
							    (leftHanded ? "LHP" : "RHP") + " profile=" + std::to_string(profile) + " zone=" + std::to_string(zone);
							Tests::Check(app->m_activePitchProfileIndex == profile && crossing.x >= left - 1e-5 &&
							    crossing.x <= left + cellWidth + 1e-5 && worldY >= top - cellHeight - 1e-5 && worldY <= top + 1e-5,
							    "Every beginner softball pitch reaches all selected zones at normal distance: " + context);
						}
					}
				}
			}
		}

		static void TestPlayerStrikeZonePersistence(const std::filesystem::path& sourceRoot)
		{
			Tests::TempDirectory output;
			const std::array<std::wstring_view, 5> presets{
			    L"PitchSettings.Difficulty.Easy.RHP.json", L"PitchSettings.Toss.Baseball.json",
			    L"PitchSettings.Toss.Softball.json", L"PitchSettings.SlowFastball.Baseball.RHP.json",
			    L"PitchSettings.SlowFastball.Softball.LHP.json"};
			const std::array<std::wstring_view, 5> environments{
			    L"EnvironmentSettings.user.json", L"EnvironmentSettings.Toss.Baseball.json",
			    L"EnvironmentSettings.Toss.Softball.json", L"EnvironmentSettings.SlowFastball.Baseball.json",
			    L"EnvironmentSettings.SlowFastball.Softball.json"};
			std::array<std::string, 5> environmentContents{};
			for (std::size_t index = 0; index < presets.size(); ++index)
			{
				Tests::Check(std::filesystem::copy_file(sourceRoot / presets[index], output.path / presets[index]) &&
				    std::filesystem::copy_file(sourceRoot / environments[index], output.path / environments[index]),
				    "Copy normal, toss and slow-fastball settings into the isolated player directory");
				environmentContents[index] = Tests::ReadFile(output.path / environments[index]);
			}
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			app->m_userDataDirectoryPath = output.path.wstring();
			app->m_strikeZoneSettings.widthMeters = .46;
			app->BeginStrikeZoneCalibration(L"test_player_zone");
			for (int frame = 0; frame <= 270; ++frame)
			{
				xr->floorSample = {1.2 * Common::EyeHeightToFullHeightFraction,
				    static_cast<double>(frame) / 90, 1, true, true};
				app->UpdateStrikeZoneCalibration();
			}
			Tests::Check(app->m_strikeZoneCalibrator.IsComplete(), "Calibrate the player zone before switching presets");
			for (const auto preset : presets)
			{
				Tests::Check(app->LoadUserPresetFromJsonFile((output.path / preset).wstring()) &&
				    app->LoadEnvironmentSettingsFromJson() && Near(app->m_strikeZoneSettings.bottomMeters, .336) &&
				    Near(app->m_strikeZoneSettings.heightMeters, .504) &&
				    Near(app->m_strikeZoneSettings.widthMeters, .46) &&
				    Near(Common::GetStrikeZoneBottomMeters(), .336) && Near(Common::GetStrikeZoneTopMeters(), .84),
				    "Normal, toss and slow-fastball environment loads preserve the calibrated player zone");
			}
			{
				auto restarted = MakeApplication();
				restarted->m_userDataDirectoryPath = output.path.wstring();
				Tests::Check(restarted->LoadPlaySettingsFromJson() && restarted->m_strikeZoneConfigured &&
				    Near(restarted->m_strikeZoneSettings.bottomMeters, .336) &&
				    Near(restarted->m_strikeZoneSettings.heightMeters, .504) &&
				    Near(restarted->m_strikeZoneSettings.widthMeters, .46) &&
				    restarted->BuildXrUiTelemetry().strikeZoneCalibration.status == Common::StrikeZoneCalibrationStatus::Saved,
				    "A completed automatic calibration survives restart as the saved player zone");
			}
			app->BeginStrikeZoneCalibration(L"test_cancel_pending_calibration");
			{
				auto restarted = MakeApplication();
				restarted->m_userDataDirectoryPath = output.path.wstring();
				Tests::Check(restarted->LoadPlaySettingsFromJson() && !restarted->m_strikeZoneConfigured &&
				    !restarted->m_strikeZoneCalibrator.IsComplete() && Near(Common::GetStrikeZoneBottomMeters(), .336) &&
				    Near(Common::GetStrikeZoneTopMeters(), .84),
				    "Restart during an explicitly requested recalibration resumes sampling while retaining the previous zone");
			}
			for (int frame = 0; frame <= 90; ++frame)
			{
				xr->floorSample = {1.8 * Common::EyeHeightToFullHeightFraction, 4 + static_cast<double>(frame) / 90, 1, true, true};
				app->UpdateStrikeZoneCalibration();
			}
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneBottomDown});
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), .326) && Near(Common::GetStrikeZoneTopMeters(), .84),
			    "Lower-bound down moves one centimetre while preserving the upper bound");
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneBottomUp});
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), .336) && Near(Common::GetStrikeZoneTopMeters(), .84),
			    "Lower-bound up moves one centimetre while preserving the upper bound");
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneTopDown});
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), .336) && Near(Common::GetStrikeZoneTopMeters(), .83),
			    "Upper-bound down moves one centimetre while preserving the lower bound");
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneTopUp});
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), .336) && Near(Common::GetStrikeZoneTopMeters(), .84) &&
			    app->m_strikeZoneCalibrator.IsComplete() &&
			    app->BuildXrUiTelemetry().strikeZoneCalibration.status == Common::StrikeZoneCalibrationStatus::Manual &&
			    Near(app->BuildXrUiTelemetry().strikeZoneCalibration.fraction, 1),
			    "Manual boundary edits cancel pending calibration without claiming an automatic calibration completed");
			for (int frame = 0; frame <= 300; ++frame)
			{
				xr->floorSample.sampleTimeSeconds = 6 + static_cast<double>(frame) / 90;
				app->UpdateStrikeZoneCalibration();
			}
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), .336) && Near(Common::GetStrikeZoneTopMeters(), .84),
			    "Continued HMD samples cannot overwrite the manual player zone");
			struct BoundaryCase { Common::StrikeZoneRuntimeSettings initial; Xr::UiCommandType command; double bottom, top; };
			const std::array cases{
			    BoundaryCase{{.205, .8, .44}, Xr::UiCommandType::AdjustStrikeZoneBottomDown, .2, 1.005},
			    BoundaryCase{{.2, .205, .44}, Xr::UiCommandType::AdjustStrikeZoneBottomUp, .205, .405},
			    BoundaryCase{{.9, 1.595, .44}, Xr::UiCommandType::AdjustStrikeZoneTopUp, .9, 2.5},
			    BoundaryCase{{.5, .205, .44}, Xr::UiCommandType::AdjustStrikeZoneTopDown, .5, .7},
			    BoundaryCase{{.5, 1.595, .44}, Xr::UiCommandType::AdjustStrikeZoneBottomDown, .495, 2.095}};
			for (const auto& test : cases)
			{
				app->m_strikeZoneSettings = test.initial;
				app->HandleXrUiCommand({test.command});
				std::wstring error;
				const auto saved = app->m_runtimeSettingsJsonStorage.LoadPlay((output.path / "PlaySettings.user.json").wstring(), error);
				Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), test.bottom) && Near(Common::GetStrikeZoneTopMeters(), test.top),
				    "Manual commands enforce floor, ceiling and height bounds while keeping the opposite edge fixed");
				Tests::Check(saved && saved->strikeZoneConfigured && Near(saved->strikeZoneBottomMeters, test.bottom) &&
				    Near(saved->strikeZoneBottomMeters + saved->strikeZoneHeightMeters, test.top),
				    "Every clamped boundary adjustment remains valid and is persisted immediately");
			}
			SetUpTee(*app);
			app->m_strikeZoneSettings = {.5, .8, .44};
			app->m_duelTargetZoneMask = 1U << 12;
			app->m_teeBattedMetrics.hasContact = true;
			app->m_teeBattedMetrics.launchVelocity = {0, 30, 0};
			app->m_battedContactSequence = 11;
			app->ApplyStrikeZoneSettingsChanges(false);
			const auto teePosition = app->m_teeBallPosition;
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneTopUp});
			Tests::Check(Near(app->m_teeBallPosition.y, teePosition.y + .005) &&
			    app->m_teeBattedMetrics.hasContact && Near(Physics::Length(app->m_teeBattedMetrics.launchVelocity), 30) &&
			    app->m_battedContactSequence == 11,
			    "Moving the zone updates a selected stationary tee without discarding the previous hit or its trail sequence");
			app->m_isTeeBallInFlight = true;
			const auto activeBall = app->m_pitchSimulationService.GetBallState();
			app->HandleXrUiCommand({Xr::UiCommandType::AdjustStrikeZoneBottomDown});
			Tests::Check(app->m_isTeeBallInFlight &&
			    SamePosition(app->m_pitchSimulationService.GetBallState().position, activeBall.position),
			    "Moving the zone during a tee hit never respawns the active ball");
			const auto savedZone = app->m_strikeZoneSettings;
			for (const auto preset : presets)
				Tests::Check(app->LoadUserPresetFromJsonFile((output.path / preset).wstring()) &&
				    app->LoadEnvironmentSettingsFromJson() && Near(app->m_strikeZoneSettings.bottomMeters, savedZone.bottomMeters) &&
				    Near(app->m_strikeZoneSettings.heightMeters, savedZone.heightMeters),
				    "Manual player boundaries survive every normal, toss and slow-fastball preset load");
			auto restarted = MakeApplication(&xr);
			restarted->m_userDataDirectoryPath = output.path.wstring();
			Tests::Check(restarted->LoadPlaySettingsFromJson() &&
			    Near(Common::GetStrikeZoneBottomMeters(), savedZone.bottomMeters) &&
			    Near(Common::GetStrikeZoneTopMeters(), savedZone.bottomMeters + savedZone.heightMeters),
			    "An application restart restores the player's independent strike-zone boundaries");
			for (std::size_t index = 0; index < environments.size(); ++index)
				Tests::Check(Tests::ReadFile(output.path / environments[index]) == environmentContents[index],
				    "Calibration and manual player-zone changes never modify preset environment files");
		}

		static void Run(const std::filesystem::path& sourceRoot)
		{
			const RuntimeSettingsGuard settingsGuard;
			TestTeeCollisionAndPause();
			TestBatGripAndFullLength();
			TestBatTrackingDiscontinuities();
			TestObliqueBatCollisionIntegration();
			TestEnvironmentReset();
			TestDuelReset();
			TestGroundOffset();
			TestGroundCarryInterpolationAndFirstBounce();
			TestDuelBoardLifecycle();
			TestFixedPitchVisibilityAndTrajectoryBoundary();
			TestActualDuelPlateDecisionAndResultRetention();
			TestLowFrameRateCurvedPitchAnalysis();
			TestTeeBoardResetLifecycle();
			TestCalibrationIntegration();
			TestIndependentTrajectoryCommandsAndPersistence();
			TestBatMaterialSelectionAndPersistence();
			TestPitcherHandednessAndPresetTempoLifecycle();
			TestRequiredSettingsProvisioning();
			TestTossFileSwitchingAndBallProperties();
			TestSlowFastballFilesAndZoneTargeting(sourceRoot);
			TestAdditionalTrainingPresets(sourceRoot);
			TestBeginnerSoftballZoneTargeting(sourceRoot);
			TestPlayerStrikeZonePersistence(sourceRoot);
		}
	};
}

void RunApplicationSimulationTests(const std::filesystem::path& sourceRoot)
{
	BaseballSimulatorVR::Core::ApplicationTestAccess::Run(sourceRoot);
}

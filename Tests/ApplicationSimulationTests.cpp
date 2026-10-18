#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Core/Application.hpp"
#include "../BaseballSimulatorVR/Common/DebugWorldProjection.hpp"
#include "../BaseballSimulatorVR/Common/PitcherPresentation.hpp"

#include <cmath>
#include <array>
#include <limits>
#include <memory>

namespace
{
	using namespace BaseballSimulatorVR;

	class TestXrSystem final : public Xr::IXrSystem
	{
	  public:
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
		Xr::HandPoseState GetLeftHandPose() const noexcept override { return {}; }
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
			xr.rightHand = { true, time, 1, { x, 1.0, -2.1 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
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
			    !app->m_hasPreviousTeeBallState && !app->m_teeBattedMetrics.hasContact &&
			    app->m_teePostBounceElapsedSeconds == 0.0,
			    "Changing the environment resets tee flight and collision history together");
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
			app->m_pitchSimulationService.Tick(flightStep);
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
			Tests::Check(released.ballVisible && released.statistics.pitchMetricsVisible &&
			    released.ballFlightPhase == Common::BallFlightPhase::Pitch &&
			    released.statistics.pitchName == L"当該フォーシーム",
			    "The actual pitch becomes visible exactly at release without preview enabled");

			app->m_activePitchProfileIndex = 1;
			app->m_activePitchParameters.initialVelocity = { 0.0, 0.0, 20.0 };
			app->m_activePitchParameters.spinRpm = 500.0;
			app->m_activePitchParameters.releaseHeightMeters = 3.0;
			const auto frozen = app->BuildFieldPresentationState();
			Tests::Check(frozen.statistics.pitchName == released.statistics.pitchName &&
			    Near(frozen.statistics.pitchSpeedMetersPerSecond, released.statistics.pitchSpeedMetersPerSecond) &&
			    frozen.statistics.pitchSpinRpm == released.statistics.pitchSpinRpm &&
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
			    !app->BuildXrUiTelemetry().hasBattedMetrics && !nextPitch.statistics.pitchMetricsVisible,
			    "Starting the next random duel pitch clears previous results on both board and telemetry");
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
			Tests::Check(app->BuildFieldPresentationState().statistics.pitchName == L"次のスプリット",
			    "The next release publishes its own frozen pitch name");
			app->m_duelBattedMetrics.hasContact = true;
			app->m_duelBattedMetrics.launchVelocity = { 50.0, 0.0, 0.0 };
			app->ResetPitchSimulation();
			Tests::Check(!app->BuildFieldPresentationState().statistics.hasBattedMetrics &&
			    !app->BuildXrUiTelemetry().hasBattedMetrics,
			    "An explicit duel reset cannot retain a previous launch result");
		}

		static void TestTeeBoardResetLifecycle()
		{
			auto app = MakeApplication();
			SetUpTee(*app);
			const auto seedContact = [&]
			{
				app->m_isTeeBallInFlight = true;
				app->m_teeBattedMetrics.hasContact = true;
				app->m_teeBattedMetrics.launchVelocity = { 18.0, 24.0, 0.0 };
			};
			const auto checkNewBall = [&]
			{
				const auto presentation = app->BuildFieldPresentationState();
				Tests::Check(!presentation.statistics.hasBattedMetrics &&
				    presentation.statistics.battedLaunchSpeedMetersPerSecond == 0.0 &&
				    !presentation.battedContactWorld &&
				    presentation.ballFlightPhase == Common::BallFlightPhase::Stationary &&
				    !app->BuildXrUiTelemetry().hasBattedMetrics,
				    "A fresh tee ball has no stale launch speed, contact point, or telemetry result");
			};
			seedContact();
			Tests::Check(Near(app->BuildFieldPresentationState().statistics.battedLaunchSpeedMetersPerSecond, 30.0),
			    "The active tee ball shows its own stored launch speed");
			app->ResetPitchSimulation();
			checkNewBall();
			seedContact();
			app->AdjustTeeBallPosition(0.05, 0.0, 0.0);
			checkNewBall();
			seedContact();
			app->RefreshTeeBallSpawnFromZoneSelection(L"test_board_tee_spawn");
			checkNewBall();
			seedContact();
			app->m_hasTeeBattedBallBounced = true;
			app->m_teePostBounceElapsedSeconds = 100.0;
			Physics::BallState settled{};
			settled.position = { 0.0, 0.037, app->m_environmentSettings.pitchDistanceMeters };
			app->m_pitchSimulationService.SetBallState(settled);
			(void)UpdateBall(*app);
			checkNewBall();
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
			const double previousZoneHeight = app->m_environmentSettings.strikeZoneHeightMeters;
			xr->floorSample = { 1.2 * Common::EyeHeightToFullHeightFraction, 0.0, 1, true, true };
			app->UpdateStrikeZoneCalibration();
			Tests::Check(Near(app->m_environmentSettings.strikeZoneHeightMeters, previousZoneHeight),
			             "Starting asynchronous calibration preserves the previous zone until it completes");
			for (int frame = 1; frame <= 270; ++frame)
			{
				xr->floorSample.sampleTimeSeconds = static_cast<double>(frame) / 90.0;
				app->UpdateStrikeZoneCalibration();
			}
			Tests::Check(app->m_strikeZoneCalibrator.IsComplete() &&
			    Near(app->m_environmentSettings.strikeZoneBottomMeters, 0.324) &&
			    Near(app->m_environmentSettings.strikeZoneHeightMeters, 0.318),
			    "Application calibration preserves a 1.2-meter player's proportional zone without a 0.40-meter clamp");
			Tests::Check(Near(Common::GetStrikeZoneBottomMeters(), 0.324) &&
			    Near(Common::GetStrikeZoneTopMeters(), 0.642),
			    "Calibration updates the geometry used by rendering and zone decisions");
			Tests::Check(std::filesystem::exists(output.path / "environment.json"),
			             "Completed calibration persists its environment settings in the selected destination");
			xr->floorSample.heightMeters = 1.9;
			xr->floorSample.sampleTimeSeconds = 4.0;
			app->UpdateStrikeZoneCalibration();
			Tests::Check(Near(app->m_environmentSettings.strikeZoneHeightMeters, 0.318),
			             "After calibration later HMD posture cannot continuously resize the zone");
			app->BeginStrikeZoneCalibration(L"test_explicit_recalibration");
			Tests::Check(!app->m_strikeZoneCalibrator.IsComplete() &&
			    Near(app->m_environmentSettings.strikeZoneHeightMeters, 0.318),
			    "Explicit recalibration clears sampling state while preserving the last usable zone");
		}

		static void TestIndependentTrajectoryCommandsAndPersistence()
		{
			Tests::TempDirectory output;
			TestXrSystem* xr = nullptr;
			auto app = MakeApplication(&xr);
			app->m_userDataDirectoryPath = output.path.wstring();
			app->m_overlayVisibility.showPitchTrajectory = true;
			app->m_overlayVisibility.showBattedTrajectory = true;
			app->HandleKeyDown('1');
			Tests::Check(!app->m_overlayVisibility.showPitchTrajectory &&
			    app->m_overlayVisibility.showBattedTrajectory,
			    "Key 1 toggles only the pitching trajectory");
			for (const auto mode : { Application::OperationMode::BattingDuel,
			    Application::OperationMode::TeeBatting, Application::OperationMode::PitchTrajectory })
			{
				app->SetOperationMode(mode, L"test_independent_trails");
				Tests::Check(!app->m_overlayVisibility.showPitchTrajectory &&
				    app->m_overlayVisibility.showBattedTrajectory,
				    "Mode switches preserve both independent trajectory preferences");
			}
			app->HandleKeyDown('4');
			Tests::Check(!app->m_overlayVisibility.showPitchTrajectory &&
			    !app->m_overlayVisibility.showBattedTrajectory,
			    "Key 4 toggles only the batted trajectory");
			const auto settingsPath = output.path / "PlaySettings.user.json";
			std::wstring error;
			const auto saved = app->m_runtimeSettingsJsonStorage.LoadPlay(settingsPath.wstring(), error);
			Tests::Check(saved && !saved->pitchTrajectoryVisible && !saved->battedTrajectoryVisible,
			    "The two hidden-trajectory preferences persist independently in PlaySettings JSON");
			const auto json = Tests::ReadFile(settingsPath);
			Tests::Check(json.find("\"pitchTrajectoryVisible\"") != std::string::npos &&
			    json.find("\"battedTrajectoryVisible\"") != std::string::npos &&
			    json.find("\"trajectoryVisible\"") == std::string::npos,
			    "PlaySettings uses the two explicit trajectory keys without a legacy combined key");
			app->HandleXrUiCommand({ Xr::UiCommandType::TogglePitchTrajectoryVisibility });
			Tests::Check(app->m_overlayVisibility.showPitchTrajectory &&
			    !app->m_overlayVisibility.showBattedTrajectory,
			    "The XR pitching command cannot toggle the batted trajectory");
			app->HandleXrUiCommand({ Xr::UiCommandType::ToggleBattedTrajectoryVisibility });
			Tests::Check(app->m_overlayVisibility.showPitchTrajectory &&
			    app->m_overlayVisibility.showBattedTrajectory &&
			    xr->overlayVisibility.showPitchTrajectory && xr->overlayVisibility.showBattedTrajectory,
			    "The XR batted command updates the two independent render preferences");
			const auto* desktop = static_cast<TestRenderBackend*>(app->m_renderBackend.get());
			Tests::Check(desktop->overlayVisibility.showPitchTrajectory &&
			    desktop->overlayVisibility.showBattedTrajectory,
			    "Desktop and XR receive the same independent trajectory settings");
			app->HandleXrUiCommand({ Xr::UiCommandType::TogglePitchTrajectoryVisibility });
			auto reloaded = MakeApplication();
			reloaded->m_userDataDirectoryPath = output.path.wstring();
			Tests::Check(reloaded->LoadPlaySettingsFromJson() &&
			    !reloaded->m_overlayVisibility.showPitchTrajectory &&
			    reloaded->m_overlayVisibility.showBattedTrajectory,
			    "Reloading PlaySettings restores a mixed trajectory preference without mode-dependent coupling");
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
			app->m_environmentSettings.moundHeightOffsetMeters = 0.0;
			// Fixture: 0.85 m bat, 0.032 m radius, normal COR 0.5.
			app->m_environmentSettings.batLengthMeters = 0.85;
			app->m_environmentSettings.batRadiusMeters = 0.032;
			app->m_environmentSettings.batBallRestitution = 0.5;
			app->m_activePitchParameters.initialVelocity = { 0.0, 0.0, 40.0 };
			app->m_activePitchParameters.spinAxis = { 1.0, 0.0, 0.0 };
			app->m_activePitchParameters.spinRpm = 2300.0;
			app->m_pitchProfiles = { { L"実際に投じた球種", app->m_activePitchParameters } };
			app->m_activePitchProfileIndex = 0;
			app->ResetPitchSimulation();
			(void)app->m_pitchDeliveryController.Advance(std::chrono::duration<double>(1.2));
			const auto before = app->BuildFieldPresentationState().statistics;
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
			xr->rightHand = { true, 3.0, 1, { -0.70, 0.98, ballWorldZ + 0.2 },
			    { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };
			app->RefreshBatPoseFromXrInput();
			xr->rightHand.position.z -= 0.4;
			xr->rightHand.sampleTimeSeconds = 3.01;
			app->RefreshBatPoseFromXrInput();
			app->m_pitchSimulationService.SetBallState(ball);
			Tests::Check(app->TryApplyBatCollision(&ball), "A real swept oblique duel contact reaches the production collision solver");
			Tests::Check(ball.aerodynamicsModel == Physics::BallAerodynamicsModel::BattedBall &&
			    ball.spinRpm > 0.0 && !Near(ball.spinRpm, 2300.0) &&
			    Near(Physics::Length(ball.spinAxis), 1.0),
			    "Contact switches to measured batted-ball aerodynamics with the impulse-derived spin");
			const auto after = app->BuildFieldPresentationState().statistics;
			Tests::Check(after.pitchName == before.pitchName &&
			    Near(after.pitchSpeedMetersPerSecond, before.pitchSpeedMetersPerSecond) &&
			    Near(after.pitchSpinRpm, before.pitchSpinRpm) && after.hasBattedMetrics &&
			    !Near(after.battedLaunchSpeedMetersPerSecond, before.pitchSpeedMetersPerSecond),
			    "Real contact preserves delivered pitch name/speed/spin while publishing the new batted launch speed");
			const auto& frozen = app->m_pitchSimulationService.GetPitchParameters();
			Tests::Check(Near(Physics::Length(frozen.initialVelocity), 40.0) && Near(frozen.spinRpm, 2300.0),
			    "Writing the collision state cannot overwrite the pitch's frozen parameters");
			app->ResetPitchSimulation();
			Tests::Check(app->m_pitchSimulationService.GetBallState().aerodynamicsModel ==
			    Physics::BallAerodynamicsModel::PitchProfile,
			    "The next pitch resets the flight model together with velocity, spin and collision history");
		}

		static void TestRequiredSettingsProvisioning()
		{
			Tests::TempDirectory output;
			const auto bundled = output.path / "bundled";
			const auto user = output.path / "user";
			Tests::Check(std::filesystem::create_directory(bundled) &&
			    std::filesystem::create_directory(user), "Create isolated bundled and user settings roots");
			const std::array<std::string_view, 12> names{
			    "PitchSettings.user.json", "PitchSettings.Yamamoto.json",
			    "PitchSettings.Difficulty.Easy.RHP.json", "PitchSettings.Difficulty.Easy.LHP.json",
			    "PitchSettings.Difficulty.Normal.RHP.json", "PitchSettings.Difficulty.Normal.LHP.json",
			    "PitchSettings.Difficulty.Hard.RHP.json", "PitchSettings.Difficulty.Hard.LHP.json",
			    "PitchSettings.Difficulty.Expert.RHP.json", "PitchSettings.Difficulty.Expert.LHP.json",
			    "EnvironmentSettings.user.json", "PlaySettings.user.json" };
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
			    "A new installation provisions every required file including all eight difficulty presets");
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
			    Near(presentation.statistics.pitchSpeedMetersPerSecond, 0.0),
			    "The fastest profile sets delivery tempo even while a slower active pitch is concealed");
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

		static void Run()
		{
			const RuntimeSettingsGuard settingsGuard;
			TestTeeCollisionAndPause();
			TestBatTrackingDiscontinuities();
			TestObliqueBatCollisionIntegration();
			TestEnvironmentReset();
			TestDuelReset();
			TestGroundOffset();
			TestGroundCarryInterpolationAndFirstBounce();
			TestDuelBoardLifecycle();
			TestTeeBoardResetLifecycle();
			TestCalibrationIntegration();
			TestIndependentTrajectoryCommandsAndPersistence();
			TestPitcherHandednessAndPresetTempoLifecycle();
			TestRequiredSettingsProvisioning();
		}
	};
}

void RunApplicationSimulationTests()
{
	BaseballSimulatorVR::Core::ApplicationTestAccess::Run();
}

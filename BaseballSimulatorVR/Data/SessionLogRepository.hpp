#pragma once

#include "../Baseball/PitchParameters.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Data
{
	struct OperationResult final
	{
		bool succeeded{ false };
		std::wstring errorMessage{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return succeeded;
		}
	};

	struct PitchEventLogResult final
	{
		std::optional<std::int64_t> eventId{};
		std::wstring errorMessage{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return eventId.has_value();
		}
	};

	// Stored in pitch_events.event_category. Summary views only aggregate Pitch events.
	enum class SessionEventCategory
	{
		Config,
		Pitch,
		BattedBall
	};

	[[nodiscard]] std::wstring_view GetSessionEventCategoryToken(SessionEventCategory category) noexcept;
	// Category of events logged through the generic LogPitchEvent overloads (and of rows written
	// before event_category existed): pitch deliveries, bat contacts, everything else is Config.
	[[nodiscard]] SessionEventCategory ClassifySessionEventType(std::wstring_view eventType, std::wstring_view note) noexcept;

	// Normalized row of the pitches table, written together with a Pitch event.
	struct PitchDetails final
	{
		std::wstring mode{};
		std::wstring profileName{};
		std::wstring pitchSettingsFile{};
		bool pitcherThrowsLeftHanded{ false };
	};

	// Result of a delivery at the front of home plate (heights above the ground, metres).
	struct PitchOutcome final
	{
		std::optional<double> plateCrossingX{};
		std::optional<double> plateCrossingY{};
		std::wstring zoneCall{};
		std::optional<double> horizontalBreakMeters{};
		std::optional<double> verticalBreakMeters{};
	};

	// Normalized row of the batted_balls table, written together with a "bat_contact" event.
	struct BattedBallDetails final
	{
		std::wstring mode{};
		// Pitch event that was hit (absent for tee batting).
		std::optional<std::int64_t> pitchEventId{};
		double exitSpeedMetersPerSecond{ 0.0 };
		double launchAngleDegrees{ 0.0 };
		// 0 = straight back over the pitcher, positive toward +x (batter's right, as windVelocityX).
		double sprayAngleDegrees{ 0.0 };
		// Known after the first ground contact; see UpdateBattedBallLanding.
		std::optional<double> carryDistanceMeters{};
		// Impact position along the bat, 0 = knob, 1 = tip.
		double contactFractionAlongBat{ 0.0 };
		std::wstring batMaterial{};
		bool batRightHanded{ true };
	};

	// SQLite session history. The database runs in WAL mode with synchronous=NORMAL; every write is
	// validated on the calling thread, assigned its event id immediately and then queued for a
	// background writer thread, so logging never blocks the frame on disk I/O. Event ids come from
	// blocks reserved in sqlite_sequence, so concurrent instances sharing a database never collide.
	class SessionLogRepository final
	{
	  public:
		SessionLogRepository();
		SessionLogRepository(const SessionLogRepository&) = delete;
		SessionLogRepository& operator=(const SessionLogRepository&) = delete;
		SessionLogRepository(SessionLogRepository&&) = delete;
		SessionLogRepository& operator=(SessionLogRepository&&) = delete;

		~SessionLogRepository();

		static constexpr int CurrentSchemaVersion = 3;

		[[nodiscard]] OperationResult Initialize(std::wstring_view databasePath);
		[[nodiscard]] bool Initialize(std::wstring_view databasePath, std::wstring& errorMessage);
		// Flushes pending writes, stops the writer thread and closes the database.
		void Shutdown();
		[[nodiscard]] bool IsInitialized() const noexcept;
		// Waits until every queued write is committed. Reports the first background write error
		// since the previous Flush.
		[[nodiscard]] OperationResult Flush();
		// Flushes first, then writes BOM + CRLF + quoted CSV of the current session atomically.
		[[nodiscard]] OperationResult ExportCurrentSessionCsv(const std::filesystem::path& filePath);
		[[nodiscard]] std::wstring GetSessionId() const;

		// Generic event; the category is derived from the event type (ClassifySessionEventType).
		[[nodiscard]] PitchEventLogResult LogPitchEvent(
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note);
		[[nodiscard]] std::optional<std::int64_t> LogPitchEvent(
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note,
		    std::wstring& errorMessage);
		[[nodiscard]] PitchEventLogResult LogEvent(
		    SessionEventCategory category,
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note);
		// Pitch event plus its pitches row.
		[[nodiscard]] PitchEventLogResult LogPitch(
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note,
		    const PitchDetails& details);
		[[nodiscard]] OperationResult RecordPitchOutcome(std::int64_t pitchEventId, const PitchOutcome& outcome);
		// "bat_contact" event plus its batted_balls row; returns the event id.
		[[nodiscard]] PitchEventLogResult LogBattedBall(
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note,
		    const BattedBallDetails& details);
		// Records where the batted ball came down and the scorer's call for it.
		[[nodiscard]] OperationResult UpdateBattedBallLanding(
		    std::int64_t battedBallEventId,
		    double carryDistanceMeters,
		    double maxHeightMeters,
		    std::wstring_view resultLabel = {});
		[[nodiscard]] OperationResult UpsertPitchEventResult(
		    std::int64_t eventId,
		    std::wstring_view resultLabel,
		    double resultScore,
		    std::wstring_view note);
		[[nodiscard]] bool UpsertPitchEventResult(
		    std::int64_t eventId,
		    std::wstring_view resultLabel,
		    double resultScore,
		    std::wstring_view note,
		    std::wstring& errorMessage);

	  private:
		struct Implementation;
		std::unique_ptr<Implementation> m_implementation;
	};
} // namespace BaseballSimulatorVR::Data

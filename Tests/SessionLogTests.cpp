#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/SessionLogRepository.hpp"

#include <sqlite3.h>

#include <limits>
#include <set>
#include <vector>

namespace
{
	using namespace BaseballSimulatorVR;

	struct RawDatabase final
	{
		sqlite3* handle{ nullptr };
		explicit RawDatabase(const std::filesystem::path& path)
		{
			const auto utf8 = path.u8string();
			Tests::Check(sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &handle) == SQLITE_OK,
			    "Open the database directly");
		}
		RawDatabase(const RawDatabase&) = delete;
		RawDatabase& operator=(const RawDatabase&) = delete;
		~RawDatabase()
		{
			sqlite3_close(handle);
		}
		void Execute(const char* sql) const
		{
			Tests::Check(sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK, sql);
		}
		[[nodiscard]] std::string Text(const char* sql) const
		{
			sqlite3_stmt* statement = nullptr;
			Tests::Check(sqlite3_prepare_v2(handle, sql, -1, &statement, nullptr) == SQLITE_OK, sql);
			std::string value{};
			if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_type(statement, 0) != SQLITE_NULL)
				value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
			sqlite3_finalize(statement);
			return value;
		}
	};

	void CheckLegacyMigrationAndViews(const std::filesystem::path& directory)
	{
		const auto path = directory / L"legacy.sqlite3";
		{
			// A session database in an earlier release's schema, with rows as that release wrote them.
			RawDatabase legacy(path);
			legacy.Execute(
			    "CREATE TABLE pitch_events (id INTEGER PRIMARY KEY AUTOINCREMENT,"
			    " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), session_id TEXT NOT NULL,"
			    " event_type TEXT NOT NULL, preset_name TEXT NOT NULL, integrator_name TEXT NOT NULL, speed REAL NOT NULL,"
			    " spin_rpm REAL NOT NULL, wind_forward REAL NOT NULL, wind_cross REAL NOT NULL,"
			    " height_wind_enabled INTEGER NOT NULL, seam_enabled INTEGER NOT NULL, seam_amp REAL NOT NULL,"
			    " seam_freq REAL NOT NULL, note TEXT NOT NULL);"
			    "CREATE VIEW pitch_trial_summary AS SELECT session_id, COUNT(*) AS event_count FROM pitch_events"
			    " GROUP BY session_id;");
			legacy.Execute(
			    "INSERT INTO pitch_events (session_id, event_type, preset_name, integrator_name, speed, spin_rpm,"
			    " wind_forward, wind_cross, height_wind_enabled, seam_enabled, seam_amp, seam_freq, note) VALUES"
			    " ('old','pitch_config','Custom','Rk4',40,2200,0,0,0,0,0,0,'duel_random'),"
			    " ('old','pitch_profile','Custom','Rk4',38,2300,0,0,0,0,0,0,'profile=Slider'),"
			    " ('old','pitch_profile','Custom','Rk4',38,2300,0,0,0,0,0,0,'random_pitch=on'),"
			    " ('old','overlay_config','Custom','Rk4',0,0,0,0,0,0,0,0,'cycle_zone'),"
			    " ('old','bat_contact','Custom','Rk4',40,2200,0,0,0,0,0,0,'mode=tee');");
		}
		{
			Data::SessionLogRepository repository;
			Tests::Check(repository.Initialize(path.wstring()).succeeded, "A database from an earlier release is migrated");
			Tests::Check(repository.UpsertPitchEventResult(1, L"strike", 1.0, L"after upgrade").succeeded,
			    "Results can still be attached to events of earlier sessions");
			Tests::Check(!repository.UpsertPitchEventResult(99, L"strike", 1.0, L"").succeeded,
			    "Unknown historical events are rejected synchronously");
			const auto next = repository.LogPitchEvent(L"pitch_config", L"Slider", L"Rk4", Baseball::PitchParameters{}, L"new");
			Tests::Check(next.eventId && *next.eventId > 5, "New ids continue after the migrated rows");
		}
		RawDatabase migrated(path);
		Tests::Check(migrated.Text("PRAGMA user_version;") == "3", "The schema version is recorded in user_version");
		Tests::Check(migrated.Text("SELECT COUNT(*) FROM pragma_table_info('batted_balls') WHERE name = 'result';") == "1",
		    "Migrated databases gain the batted-ball result column");
		Tests::Check(migrated.Text("SELECT group_concat(event_category, ',') FROM (SELECT event_category FROM pitch_events"
		                           " WHERE session_id = 'old' ORDER BY id);") == "pitch,pitch,config,config,batted_ball",
		    "Existing rows are classified into pitch, configuration and batted-ball events");
		Tests::Check(migrated.Text("SELECT event_count FROM pitch_trial_summary WHERE session_id = 'old';") == "2" &&
		        migrated.Text("SELECT SUM(sample_count) FROM pitch_preset_comparison;") == "3",
		    "Summary views only aggregate pitch events");
		Tests::Check(migrated.Text("SELECT result_label FROM pitch_event_results WHERE event_id = 1;") == "strike",
		    "The historical result was committed");
	}

	void CheckBackgroundWriterAndNormalizedTables(const std::filesystem::path& directory)
	{
		const auto path = directory / L"normalized.sqlite3";
		Baseball::PitchParameters pitch{};
		pitch.initialVelocity = { 0.0, 0.0, 40.0 };
		std::int64_t pitchId = 0;
		std::int64_t battedId = 0;
		std::set<std::int64_t> ids{};
		{
			Data::SessionLogRepository first;
			Data::SessionLogRepository second;
			Tests::Check(first.Initialize(path.wstring()).succeeded && second.Initialize(path.wstring()).succeeded,
			    "Two repositories can share one WAL database");
			RawDatabase probe(path);
			Tests::Check(probe.Text("PRAGMA journal_mode;") == "wal", "The session database uses WAL journaling");

			Data::PitchDetails details{ L"batting-duel", L"ストレート", L"PitchSettings.Difficulty.Easy.LHP.json", true };
			const auto logged = first.LogPitch(L"pitch_config", L"ストレート", L"Rk4", pitch, L"duel_random", details);
			Tests::Check(logged.eventId.has_value(), "Pitch rows are logged with their normalized details");
			pitchId = *logged.eventId;
			Tests::Check(first.UpsertPitchEventResult(pitchId, L"ball", -1.0, L"auto").succeeded,
			    "A result for a queued (not yet committed) pitch is accepted immediately");
			Data::PitchOutcome outcome{ 0.12, 0.85, L"ball", -0.05, 0.31 };
			Tests::Check(first.RecordPitchOutcome(pitchId, outcome).succeeded, "Outcomes are queued");
			Data::BattedBallDetails batted{};
			batted.mode = L"batting-duel";
			batted.pitchEventId = pitchId;
			batted.exitSpeedMetersPerSecond = 41.5;
			batted.launchAngleDegrees = 24.0;
			batted.sprayAngleDegrees = -12.5;
			batted.contactFractionAlongBat = 0.82;
			batted.batMaterial = L"Wood";
			const auto battedResult = first.LogBattedBall(L"ストレート", L"Rk4", pitch, L"contact", batted);
			Tests::Check(battedResult.eventId.has_value(), "Batted balls are logged");
			battedId = *battedResult.eventId;
			Tests::Check(first.UpdateBattedBallLanding(battedId, 97.25, 21.0, L"センターフライ").succeeded,
			    "Landings are queued with the judged result");
			batted.contactFractionAlongBat = 1.5;
			Tests::Check(!first.LogBattedBall(L"x", L"Rk4", pitch, L"", batted).eventId.has_value(),
			    "Out-of-range batted-ball values are rejected synchronously");
			batted.contactFractionAlongBat = 0.5;
			batted.exitSpeedMetersPerSecond = std::numeric_limits<double>::quiet_NaN();
			Tests::Check(!first.LogBattedBall(L"x", L"Rk4", pitch, L"", batted).eventId.has_value(),
			    "Non-finite batted-ball values are rejected synchronously");
			Tests::Check(!first.RecordPitchOutcome(pitchId + 100000, outcome).succeeded,
			    "Outcomes for unknown pitches are rejected synchronously");

			for (int index = 0; index < 50; ++index)
			{
				auto& repository = (index % 2 == 0) ? first : second;
				const auto event = repository.LogPitchEvent(L"overlay_config", L"x", L"Rk4", pitch, L"interleaved");
				Tests::Check(event.eventId && ids.insert(*event.eventId).second,
				    "Concurrent repositories never hand out the same event id");
			}
			Tests::Check(first.Flush().succeeded && second.Flush().succeeded, "Flush commits every queued write");
			Tests::Check(probe.Text("SELECT COUNT(*) FROM pitch_events;") == "52",
			    "After Flush every queued event is visible to other connections");
			Tests::Check(probe.Text(("SELECT result_label || ',' || zone_call || ',' || pitcher_hand || ',' || mode"
			                         " FROM pitches JOIN pitch_event_results r ON r.event_id = pitches.event_id WHERE"
			                         " pitches.event_id = " + std::to_string(pitchId) + ";").c_str()) == "ball,ball,L,batting-duel",
			    "Pitch rows, outcomes and results are written in submission order");
			Tests::Check(probe.Text(("SELECT exit_speed || ',' || carry_distance || ',' || pitch_event_id || ',' ||"
			                         " contact_fraction FROM batted_balls WHERE event_id = " + std::to_string(battedId) + ";").c_str()) ==
			        "41.5,97.25," + std::to_string(pitchId) + ",0.82",
			    "Batted balls store exit speed, carry, the hit pitch and the contact position");
			Tests::Check(probe.Text(("SELECT result FROM batted_balls WHERE event_id = " + std::to_string(battedId) + ";").c_str()) ==
			        reinterpret_cast<const char*>(u8"センターフライ"),
			    "Batted balls store the judged hit or out call");
			Tests::Check(probe.Text("SELECT batted_ball_count FROM batted_ball_summary;") == "1" &&
			        probe.Text("SELECT ball_count FROM pitch_outcome_summary;") == "1",
			    "Normalized summary views aggregate pitches and batted balls");

			Tests::Check(first.UpdateBattedBallLanding(pitchId, 1.0, 1.0).succeeded,
			    "Validation passes for an id of this session");
			const auto failed = first.Flush();
			Tests::Check(!failed.succeeded && !failed.errorMessage.empty(),
			    "A failed background write is reported by the next Flush");
			Tests::Check(first.Flush().succeeded, "Reported background errors are cleared");

			const auto csvPath = directory / L"normalized.csv";
			Tests::Check(first.ExportCurrentSessionCsv(csvPath).succeeded, "Export the normalized session");
			const auto csv = Tests::ReadFile(csvPath);
			Tests::Check(csv.find("\"event_category\",\"pitch_mode\",\"profile_name\"") != std::string::npos &&
			    csv.find("\"exit_speed\"") != std::string::npos && csv.find("\"zone_call\"") != std::string::npos &&
			    csv.find("41.5") != std::string::npos && csv.find("\"batting-duel\"") != std::string::npos &&
			    csv.find("\"batted_result\"") != std::string::npos &&
			    csv.find(reinterpret_cast<const char*>(u8"\"センターフライ\"")) != std::string::npos &&
			    csv.ends_with("\r\n"), "The CSV export includes the normalized pitch and batted-ball columns");
		}
		{
			// Unused reserved ids are returned, so a later session continues right after the last event.
			Data::SessionLogRepository later;
			Tests::Check(later.Initialize(path.wstring()).succeeded, "Reopen the shared database");
			const auto event = later.LogPitchEvent(L"pitch", L"x", L"Rk4", pitch, L"");
			const std::int64_t maximum = (std::max)(*ids.rbegin(), battedId);
			Tests::Check(event.eventId && *event.eventId > maximum, "Ids stay unique across sessions");
		}
		{
			Data::SessionLogRepository sequential;
			Tests::Check(sequential.Initialize(path.wstring()).succeeded, "Open once more");
			const auto first = sequential.LogPitchEvent(L"pitch", L"x", L"Rk4", pitch, L"");
			sequential.Shutdown();
			Data::SessionLogRepository next;
			Tests::Check(next.Initialize(path.wstring()).succeeded, "Open after a clean shutdown");
			const auto second = next.LogPitchEvent(L"pitch", L"x", L"Rk4", pitch, L"");
			Tests::Check(first.eventId && second.eventId && *second.eventId == *first.eventId + 1,
			    "Sequential sessions produce contiguous ids");
		}
		Tests::Check(Data::ClassifySessionEventType(L"pitch_profile", L"profile=Slider") == Data::SessionEventCategory::Pitch &&
		    Data::ClassifySessionEventType(L"pitch_profile", L"random_pitch=on") == Data::SessionEventCategory::Config &&
		    Data::ClassifySessionEventType(L"bat_contact", L"") == Data::SessionEventCategory::BattedBall &&
		    Data::ClassifySessionEventType(L"playback_speed", L"") == Data::SessionEventCategory::Config,
		    "Event types are classified for the summary views");
	}
}

void RunSessionLogTests()
{
	using namespace BaseballSimulatorVR;
	Tests::TempDirectory directory;
	const auto databasePath = directory.path / L"ログ.sqlite3";
	const auto csvPath = directory.path / L"投球.csv";
	Data::SessionLogRepository firstSession;
	Tests::Check(!firstSession.ExportCurrentSessionCsv(csvPath), "Export requires initialized repository");
	Tests::Check(firstSession.Initialize(databasePath.wstring()).succeeded, "Initialize SQLite at Unicode path");
	Tests::Check(firstSession.ExportCurrentSessionCsv(csvPath).succeeded, "Empty session exports a header");
	const auto emptyCsv = Tests::ReadFile(csvPath);
	Tests::Check(emptyCsv.starts_with("\xEF\xBB\xBF\"id\",\"created_at\""), "UTF-8 BOM and named CSV columns");
	Tests::Check(emptyCsv.ends_with("\"result_updated_at\"\r\n"), "CSV includes result columns and CRLF");
	Tests::Check(emptyCsv.find('\n') == emptyCsv.size() - 1, "Empty export has exactly one row");

	Baseball::PitchParameters pitch;
	const auto event = firstSession.LogPitchEvent(
	    L"pitch", L"山本, \"FourSeam\"", L"Rk4", pitch, L"first\r\nsecond");
	Tests::Check(event.eventId.has_value(), "Insert event with Unicode and CSV delimiters");
	Tests::Check(firstSession.UpsertPitchEventResult(*event.eventId, L"strike", 1.25, L"判定,\"良\"").succeeded,
	             "Attach a result to event");
	Tests::Check(firstSession.ExportCurrentSessionCsv(csvPath).succeeded, "Overwrite completed export atomically");
	const auto populatedCsv = Tests::ReadFile(csvPath);
	Tests::Check(populatedCsv.find("\"山本, \"\"FourSeam\"\"\"") != std::string::npos,
	             "Unicode preserved and embedded CSV quotes doubled");
	Tests::Check(populatedCsv.find("\"first\r\nsecond\"") != std::string::npos,
	             "Newlines stay inside quoted CSV fields");
	Tests::Check(populatedCsv.find("\"strike\",1.25,\"判定,\"\"良\"\"\"") != std::string::npos,
	             "CSV contains joined result and numeric score");

	Tests::Check(!firstSession.UpsertPitchEventResult(*event.eventId, L"bad", std::numeric_limits<double>::infinity(), L""),
	             "Reject infinite result scores");
	Tests::Check(!firstSession.UpsertPitchEventResult(*event.eventId, L"bad", std::numeric_limits<double>::quiet_NaN(), L""),
	             "Reject NaN result scores");
	Tests::Check(!firstSession.UpsertPitchEventResult(*event.eventId + 100, L"bad", 1.0, L""),
	             "Foreign key rejects result without event");
	pitch.spinRpm = std::numeric_limits<double>::infinity();
	Tests::Check(!firstSession.LogPitchEvent(L"invalid", L"invalid", L"Rk4", pitch, L""),
	             "Reject nonfinite event values");
	pitch = {};
	const std::wstring invalidUnicode(1, static_cast<wchar_t>(0xD800));
	Tests::Check(!firstSession.LogPitchEvent(L"invalid", invalidUnicode, L"Rk4", pitch, L""),
	             "Reject malformed UTF-16 rather than silently replacing text");
	Tests::Check(!firstSession.UpsertPitchEventResult(*event.eventId, L"bad", 1.0, invalidUnicode),
	             "Reject malformed result note");
	const std::wstring embeddedNull(L"left\0right", 10);
	Tests::Check(firstSession.LogPitchEvent(L"embedded_null", embeddedNull, L"Rk4", pitch, L"").eventId.has_value(),
	             "Preserve complete text length on SQLite bind");
	Tests::Check(firstSession.ExportCurrentSessionCsv(csvPath).succeeded, "Export text containing null");
	Tests::Check(Tests::ReadFile(csvPath).find(std::string("left\0right", 10)) != std::string::npos,
	             "Embedded null is not truncated");
	Tests::Check(!firstSession.ExportCurrentSessionCsv(databasePath), "Export cannot replace open database");
	const auto finalCsv = Tests::ReadFile(csvPath);
	Tests::Check(!firstSession.ExportCurrentSessionCsv(directory.path / L"missing" / L"export.csv"),
	             "Export reports missing output directory");
	{
		std::ifstream lockedFile(csvPath, std::ios::binary);
		Tests::Check(lockedFile.is_open(), "Lock previous CSV for write failure test");
		Tests::Check(!firstSession.ExportCurrentSessionCsv(csvPath), "Export reports publication failure on locked target");
		Tests::Check(Tests::ReadFile(csvPath) == finalCsv, "Failed export preserves previous output");
	}

	Data::SessionLogRepository secondSession;
	Tests::Check(secondSession.Initialize(databasePath.wstring()).succeeded, "Second session shares database");
	const auto secondCsvPath = directory.path / L"second.csv";
	Tests::Check(secondSession.ExportCurrentSessionCsv(secondCsvPath).succeeded, "Export empty second session");
	Tests::Check(Tests::ReadFile(secondCsvPath) == emptyCsv, "Current-session export excludes historical sessions");
	Tests::Check(secondSession.LogPitchEvent(L"new_session", L"second", L"Rk4", pitch, L"").eventId.has_value(),
	             "Log in second session");
	Tests::Check(secondSession.ExportCurrentSessionCsv(secondCsvPath).succeeded, "Export second session event");
	const auto secondCsv = Tests::ReadFile(secondCsvPath);
	Tests::Check(secondCsv.find("new_session") != std::string::npos && secondCsv.find("FourSeam") == std::string::npos,
	             "Session query selects only its own events");
	firstSession.Shutdown();
	Tests::Check(!firstSession.ExportCurrentSessionCsv(csvPath), "Export fails after shutdown");
	for (const auto& entry : std::filesystem::directory_iterator(directory.path))
	{
		Tests::Check(entry.path().extension() != L".tmp", "Export removes temporary files after success and failure");
	}
	secondSession.Shutdown();
	CheckLegacyMigrationAndViews(directory.path);
	CheckBackgroundWriterAndNormalizedTables(directory.path);
}

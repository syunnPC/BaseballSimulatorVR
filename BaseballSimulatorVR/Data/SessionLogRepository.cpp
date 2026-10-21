#include "SessionLogRepository.hpp"

#include <sqlite3.h>

#include "../Common/AppLog.hpp"
#include "../Common/AtomicFileWriter.hpp"
#include "../Common/TextConversion.hpp"
#include "../Framework.hpp"
#include "../Physics/PitchIntegratorType.hpp"
#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		namespace Text = Common::TextConversion;

		// Ids handed out without waiting for the database; a new block is reserved in the
		// background when a quarter of the current block is left.
		constexpr std::int64_t EventIdBlockSize = 4096;
		constexpr int BusyTimeoutMilliseconds = 5000;
		constexpr std::wstring_view LogCategory = L"SessionLog";

		constexpr char CreatePitchEventTableSql[] =
		    "CREATE TABLE IF NOT EXISTS pitch_events ("
		    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
		    " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
		    " session_id TEXT NOT NULL,"
		    " event_type TEXT NOT NULL,"
		    " preset_name TEXT NOT NULL,"
		    " integrator_name TEXT NOT NULL,"
		    " speed REAL NOT NULL,"
		    " spin_rpm REAL NOT NULL,"
		    " wind_forward REAL NOT NULL,"
		    " wind_cross REAL NOT NULL,"
		    " height_wind_enabled INTEGER NOT NULL,"
		    " seam_enabled INTEGER NOT NULL,"
		    " seam_amp REAL NOT NULL,"
		    " seam_freq REAL NOT NULL,"
		    " note TEXT NOT NULL"
		    ");";

		constexpr char CreatePitchEventIndexesSql[] =
		    "CREATE INDEX IF NOT EXISTS idx_pitch_events_created_at ON pitch_events(created_at);"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_events_session_created_at ON pitch_events(session_id, created_at);"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_events_event_preset ON pitch_events(event_type, preset_name);"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_events_integrator ON pitch_events(integrator_name);";

		constexpr char CreatePitchEventResultsTableSql[] =
		    "CREATE TABLE IF NOT EXISTS pitch_event_results ("
		    " event_id INTEGER PRIMARY KEY,"
		    " result_label TEXT NOT NULL,"
		    " result_score REAL NOT NULL,"
		    " note TEXT NOT NULL,"
		    " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
		    " FOREIGN KEY(event_id) REFERENCES pitch_events(id) ON DELETE CASCADE"
		    ");"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_event_results_label ON pitch_event_results(result_label);"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_event_results_score ON pitch_event_results(result_score);";

		// Schema version 2: event categories, normalized pitch and batted-ball tables, and summary
		// views that only aggregate real pitches.
		constexpr char BackfillEventCategorySql[] =
		    "UPDATE pitch_events SET event_category = CASE"
		    " WHEN event_type IN ('pitch','pitch_config','pitch_zone','json_load') THEN 'pitch'"
		    " WHEN event_type = 'pitch_profile' AND note LIKE 'profile=%' THEN 'pitch'"
		    " WHEN event_type = 'bat_contact' THEN 'batted_ball'"
		    " ELSE 'config' END;"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_events_category ON pitch_events(event_category, session_id);";

		constexpr char CreateNormalizedTablesSql[] =
		    "CREATE TABLE IF NOT EXISTS pitches ("
		    " event_id INTEGER PRIMARY KEY REFERENCES pitch_events(id) ON DELETE CASCADE,"
		    " session_id TEXT NOT NULL,"
		    " mode TEXT NOT NULL,"
		    " profile_name TEXT NOT NULL,"
		    " pitch_settings_file TEXT NOT NULL,"
		    " pitcher_hand TEXT NOT NULL,"
		    " release_speed REAL NOT NULL,"
		    " spin_rpm REAL NOT NULL,"
		    " plate_x REAL,"
		    " plate_y REAL,"
		    " zone_call TEXT,"
		    " horizontal_break REAL,"
		    " vertical_break REAL,"
		    " resolved_at TEXT"
		    ");"
		    "CREATE INDEX IF NOT EXISTS idx_pitches_session ON pitches(session_id, mode);"
		    "CREATE TABLE IF NOT EXISTS batted_balls ("
		    " event_id INTEGER PRIMARY KEY REFERENCES pitch_events(id) ON DELETE CASCADE,"
		    " pitch_event_id INTEGER REFERENCES pitch_events(id) ON DELETE SET NULL,"
		    " session_id TEXT NOT NULL,"
		    " mode TEXT NOT NULL,"
		    " exit_speed REAL NOT NULL,"
		    " launch_angle REAL NOT NULL,"
		    " spray_angle REAL NOT NULL,"
		    " carry_distance REAL,"
		    " max_height REAL,"
		    " contact_fraction REAL NOT NULL,"
		    " bat_material TEXT NOT NULL,"
		    " bat_hand TEXT NOT NULL"
		    ");"
		    "CREATE INDEX IF NOT EXISTS idx_batted_balls_session ON batted_balls(session_id, mode);"
		    "CREATE INDEX IF NOT EXISTS idx_batted_balls_pitch ON batted_balls(pitch_event_id);";

		constexpr char RecreateViewsSql[] =
		    "DROP VIEW IF EXISTS pitch_trial_summary;"
		    "DROP VIEW IF EXISTS pitch_preset_comparison;"
		    "DROP VIEW IF EXISTS pitch_event_comparison_with_results;"
		    "DROP VIEW IF EXISTS pitch_outcome_summary;"
		    "DROP VIEW IF EXISTS batted_ball_summary;"
		    "CREATE VIEW pitch_trial_summary AS SELECT"
		    " session_id, preset_name, integrator_name,"
		    " COUNT(*) AS event_count,"
		    " MIN(created_at) AS first_event_at,"
		    " MAX(created_at) AS last_event_at,"
		    " AVG(speed) AS avg_speed,"
		    " AVG(spin_rpm) AS avg_spin_rpm,"
		    " AVG(wind_forward) AS avg_wind_forward,"
		    " AVG(wind_cross) AS avg_wind_cross,"
		    " SUM(CASE WHEN height_wind_enabled <> 0 THEN 1 ELSE 0 END) AS height_wind_enabled_count,"
		    " SUM(CASE WHEN seam_enabled <> 0 THEN 1 ELSE 0 END) AS seam_enabled_count"
		    " FROM pitch_events WHERE event_category = 'pitch'"
		    " GROUP BY session_id, preset_name, integrator_name;"
		    "CREATE VIEW pitch_preset_comparison AS SELECT"
		    " preset_name, event_type,"
		    " COUNT(*) AS sample_count,"
		    " COUNT(DISTINCT session_id) AS session_count,"
		    " AVG(speed) AS avg_speed,"
		    " AVG(spin_rpm) AS avg_spin_rpm,"
		    " AVG(wind_forward) AS avg_wind_forward,"
		    " AVG(wind_cross) AS avg_wind_cross,"
		    " AVG(seam_amp) AS avg_seam_amp,"
		    " AVG(seam_freq) AS avg_seam_freq"
		    " FROM pitch_events WHERE event_category = 'pitch'"
		    " GROUP BY preset_name, event_type;"
		    "CREATE VIEW pitch_event_comparison_with_results AS SELECT"
		    " e.id, e.created_at, e.session_id, e.event_type, e.preset_name, e.integrator_name,"
		    " e.speed, e.spin_rpm, e.wind_forward, e.wind_cross, e.height_wind_enabled, e.seam_enabled,"
		    " e.seam_amp, e.seam_freq, e.note AS event_note, e.event_category,"
		    " p.mode AS pitch_mode, p.profile_name, p.pitch_settings_file, p.pitcher_hand,"
		    " p.plate_x, p.plate_y, p.zone_call, p.horizontal_break, p.vertical_break,"
		    " b.mode AS batted_mode, b.pitch_event_id AS batted_pitch_event_id, b.exit_speed,"
		    " b.launch_angle, b.spray_angle, b.carry_distance, b.max_height, b.contact_fraction,"
		    " b.bat_material, b.bat_hand,"
		    " r.result_label, r.result_score, r.note AS result_note, r.updated_at AS result_updated_at"
		    " FROM pitch_events e"
		    " LEFT JOIN pitches p ON p.event_id = e.id"
		    " LEFT JOIN batted_balls b ON b.event_id = e.id"
		    " LEFT JOIN pitch_event_results r ON r.event_id = e.id;"
		    "CREATE VIEW pitch_outcome_summary AS SELECT"
		    " session_id, mode, profile_name,"
		    " COUNT(*) AS pitch_count,"
		    " SUM(CASE WHEN zone_call = 'strike' THEN 1 ELSE 0 END) AS strike_count,"
		    " SUM(CASE WHEN zone_call = 'ball' THEN 1 ELSE 0 END) AS ball_count,"
		    " SUM(CASE WHEN zone_call = 'contact' THEN 1 ELSE 0 END) AS contact_count,"
		    " AVG(release_speed) AS avg_release_speed,"
		    " AVG(horizontal_break) AS avg_horizontal_break,"
		    " AVG(vertical_break) AS avg_vertical_break"
		    " FROM pitches GROUP BY session_id, mode, profile_name;"
		    "CREATE VIEW batted_ball_summary AS SELECT"
		    " session_id, mode,"
		    " COUNT(*) AS batted_ball_count,"
		    " AVG(exit_speed) AS avg_exit_speed,"
		    " MAX(exit_speed) AS max_exit_speed,"
		    " AVG(launch_angle) AS avg_launch_angle,"
		    " AVG(spray_angle) AS avg_spray_angle,"
		    " MAX(carry_distance) AS max_carry_distance,"
		    " AVG(contact_fraction) AS avg_contact_fraction"
		    " FROM batted_balls GROUP BY session_id, mode;";

		constexpr char InsertEventSql[] =
		    "INSERT INTO pitch_events (id, created_at, session_id, event_type, event_category, preset_name,"
		    " integrator_name, speed, spin_rpm, wind_forward, wind_cross, height_wind_enabled, seam_enabled,"
		    " seam_amp, seam_freq, note) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
		constexpr char InsertPitchSql[] =
		    "INSERT INTO pitches (event_id, session_id, mode, profile_name, pitch_settings_file, pitcher_hand,"
		    " release_speed, spin_rpm) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
		constexpr char UpdatePitchOutcomeSql[] =
		    "UPDATE pitches SET plate_x = ?, plate_y = ?, zone_call = ?, horizontal_break = ?,"
		    " vertical_break = ?, resolved_at = ? WHERE event_id = ?;";
		constexpr char InsertBattedBallSql[] =
		    "INSERT INTO batted_balls (event_id, pitch_event_id, session_id, mode, exit_speed, launch_angle,"
		    " spray_angle, carry_distance, contact_fraction, bat_material, bat_hand)"
		    " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
		constexpr char UpdateBattedBallLandingSql[] =
		    "UPDATE batted_balls SET carry_distance = ?, max_height = ? WHERE event_id = ?;";
		constexpr char UpsertResultSql[] =
		    "INSERT INTO pitch_event_results (event_id, result_label, result_score, note, updated_at)"
		    " VALUES (?, ?, ?, ?, ?)"
		    " ON CONFLICT(event_id) DO UPDATE SET"
		    " result_label = excluded.result_label,"
		    " result_score = excluded.result_score,"
		    " note = excluded.note,"
		    " updated_at = excluded.updated_at;";

		struct EventRow final
		{
			std::int64_t id{ 0 };
			std::string createdAt{};
			std::string eventType{};
			std::string category{};
			std::string presetName{};
			std::string integratorName{};
			std::string note{};
			double speed{ 0.0 };
			double spinRpm{ 0.0 };
			double windForward{ 0.0 };
			double windCross{ 0.0 };
			bool heightWind{ false };
			bool seam{ false };
			double seamAmplitude{ 0.0 };
			double seamFrequency{ 0.0 };
		};

		struct InsertEventJob final
		{
			EventRow row{};
		};

		struct InsertPitchJob final
		{
			std::int64_t eventId{ 0 };
			std::string mode{};
			std::string profileName{};
			std::string pitchSettingsFile{};
			std::string pitcherHand{};
			double releaseSpeed{ 0.0 };
			double spinRpm{ 0.0 };
		};

		struct PitchOutcomeJob final
		{
			std::int64_t eventId{ 0 };
			std::optional<double> plateX{};
			std::optional<double> plateY{};
			std::string zoneCall{};
			std::optional<double> horizontalBreak{};
			std::optional<double> verticalBreak{};
			std::string resolvedAt{};
		};

		struct InsertBattedBallJob final
		{
			std::int64_t eventId{ 0 };
			std::optional<std::int64_t> pitchEventId{};
			std::string mode{};
			double exitSpeed{ 0.0 };
			double launchAngle{ 0.0 };
			double sprayAngle{ 0.0 };
			std::optional<double> carryDistance{};
			double contactFraction{ 0.0 };
			std::string batMaterial{};
			std::string batHand{};
		};

		struct BattedBallLandingJob final
		{
			std::int64_t eventId{ 0 };
			double carryDistance{ 0.0 };
			double maxHeight{ 0.0 };
		};

		struct UpsertResultJob final
		{
			std::int64_t eventId{ 0 };
			std::string label{};
			double score{ 0.0 };
			std::string note{};
			std::string updatedAt{};
		};

		struct ReserveIdsJob final
		{
		};

		using Job = std::variant<InsertEventJob, InsertPitchJob, PitchOutcomeJob, InsertBattedBallJob,
		    BattedBallLandingJob, UpsertResultJob, ReserveIdsJob>;

		struct IdBlock final
		{
			std::int64_t first{ 0 };
			std::int64_t end{ 0 };
		};

		[[nodiscard]] std::string BuildUtcTimestamp()
		{
			SYSTEMTIME time{};
			GetSystemTime(&time);
			char buffer[32]{};
			(void)std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			    static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
			    static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
			    static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
			    static_cast<unsigned>(time.wMilliseconds));
			return buffer;
		}

		[[nodiscard]] std::wstring BuildSessionIdentifier()
		{
			static std::atomic<std::uint64_t> sequence{ 0 };
			FILETIME fileTime{};
			GetSystemTimeAsFileTime(&fileTime);
			ULARGE_INTEGER timestamp{};
			timestamp.LowPart = fileTime.dwLowDateTime;
			timestamp.HighPart = fileTime.dwHighDateTime;
			return L"pid" + std::to_wstring(GetCurrentProcessId()) +
			       L"-" + std::to_wstring(timestamp.QuadPart) +
			       L"-" + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
		}

		[[nodiscard]] std::wstring DescribeSqliteError(sqlite3* database)
		{
			return database == nullptr ? std::wstring{ L"no database" } : Text::Utf8ToWideLossy(sqlite3_errmsg(database));
		}

		[[nodiscard]] bool IsFinite(const std::optional<double>& value) noexcept
		{
			return !value.has_value() || std::isfinite(*value);
		}

		using StatementPointer = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

		[[nodiscard]] StatementPointer Prepare(sqlite3* database, const char* sql)
		{
			sqlite3_stmt* statement = nullptr;
			if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
			{
				sqlite3_finalize(statement);
				statement = nullptr;
			}
			return StatementPointer{ statement, &sqlite3_finalize };
		}

		[[nodiscard]] bool Execute(sqlite3* database, const char* sql, std::wstring& errorMessage)
		{
			char* sqliteError = nullptr;
			if (sqlite3_exec(database, sql, nullptr, nullptr, &sqliteError) == SQLITE_OK)
			{
				return true;
			}
			errorMessage = Text::Utf8ToWideLossy(sqliteError != nullptr ? sqliteError : sqlite3_errmsg(database));
			sqlite3_free(sqliteError);
			return false;
		}

		[[nodiscard]] std::optional<std::int64_t> QueryInteger(sqlite3* database, const char* sql)
		{
			const StatementPointer statement = Prepare(database, sql);
			if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW ||
			    sqlite3_column_type(statement.get(), 0) == SQLITE_NULL)
			{
				return std::nullopt;
			}
			return static_cast<std::int64_t>(sqlite3_column_int64(statement.get(), 0));
		}

		[[nodiscard]] bool BindText(sqlite3_stmt* statement, const int index, const std::string& value)
		{
			return sqlite3_bind_text64(statement, index, value.c_str(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8) ==
			       SQLITE_OK;
		}

		[[nodiscard]] bool BindOptional(sqlite3_stmt* statement, const int index, const std::optional<double>& value)
		{
			return value.has_value() ? sqlite3_bind_double(statement, index, *value) == SQLITE_OK
			                         : sqlite3_bind_null(statement, index) == SQLITE_OK;
		}

		[[nodiscard]] bool BindOptionalText(sqlite3_stmt* statement, const int index, const std::string& value)
		{
			return value.empty() ? sqlite3_bind_null(statement, index) == SQLITE_OK : BindText(statement, index, value);
		}

		[[nodiscard]] bool IsSqlIdentifier(const std::string_view identifier)
		{
			return !identifier.empty() && std::all_of(identifier.begin(), identifier.end(),
			    [](const char character) { return character == '_' || std::isalnum(static_cast<unsigned char>(character)); });
		}

		[[nodiscard]] bool EnsureColumnExists(sqlite3* database, const std::string_view table,
		    const std::string_view column, const std::string_view definition, std::wstring& errorMessage)
		{
			if (!IsSqlIdentifier(table) || !IsSqlIdentifier(column) || !definition.starts_with(column))
			{
				errorMessage = L"Invalid SQLite schema migration arguments.";
				return false;
			}
			const std::string pragma = "PRAGMA table_info(\"" + std::string{ table } + "\");";
			const StatementPointer statement = Prepare(database, pragma.c_str());
			if (!statement)
			{
				errorMessage = L"Failed to inspect SQLite table info: " + DescribeSqliteError(database);
				return false;
			}
			int step = SQLITE_ROW;
			while ((step = sqlite3_step(statement.get())) == SQLITE_ROW)
			{
				const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
				if (name != nullptr && column == name)
				{
					return true;
				}
			}
			if (step != SQLITE_DONE)
			{
				errorMessage = L"Failed while reading SQLite table info: " + DescribeSqliteError(database);
				return false;
			}
			const std::string alter = "ALTER TABLE \"" + std::string{ table } + "\" ADD COLUMN " + std::string{ definition } + ";";
			return Execute(database, alter.c_str(), errorMessage);
		}
	} // namespace

	std::wstring_view GetSessionEventCategoryToken(const SessionEventCategory category) noexcept
	{
		switch (category)
		{
		case SessionEventCategory::Pitch:
			return L"pitch";
		case SessionEventCategory::BattedBall:
			return L"batted_ball";
		case SessionEventCategory::Config:
		default:
			return L"config";
		}
	}

	SessionEventCategory ClassifySessionEventType(const std::wstring_view eventType, const std::wstring_view note) noexcept
	{
		if (eventType == L"pitch" || eventType == L"pitch_config" || eventType == L"pitch_zone" ||
		    eventType == L"json_load" || (eventType == L"pitch_profile" && note.starts_with(L"profile=")))
		{
			return SessionEventCategory::Pitch;
		}
		if (eventType == L"bat_contact")
		{
			return SessionEventCategory::BattedBall;
		}
		return SessionEventCategory::Config;
	}

	struct SessionLogRepository::Implementation final
	{
		sqlite3* database{ nullptr };
		std::wstring sessionId{};
		std::string sessionIdUtf8{};

		// Serializes use of the connection between the writer thread and synchronous readers.
		std::mutex databaseMutex{};
		// Writer-thread statement cache, keyed by SQL text address.
		std::map<const char*, sqlite3_stmt*> statements{};

		std::mutex mutex{};
		std::condition_variable workAvailable{};
		std::condition_variable progress{};
		std::vector<Job> queue{};
		std::uint64_t enqueuedJobs{ 0U };
		std::uint64_t completedJobs{ 0U };
		bool stopRequested{ false };
		bool workerRunning{ false };
		std::thread worker{};
		std::wstring firstBackgroundError{};
		std::size_t backgroundErrorCount{ 0U };

		std::int64_t nextEventId{ 0 };
		std::int64_t blockEnd{ 0 };
		std::optional<IdBlock> pendingBlock{};
		bool reservationRequested{ false };
		IdBlock latestReservation{};
		std::vector<IdBlock> issuedRanges{};

		~Implementation()
		{
			Close();
		}

		[[nodiscard]] bool IsOpen() const noexcept
		{
			return database != nullptr;
		}

		void RecordBackgroundError(std::wstring message)
		{
			Common::AppLog::Error(LogCategory, message);
			const std::lock_guard lock(mutex);
			if (firstBackgroundError.empty())
			{
				firstBackgroundError = std::move(message);
			}
			++backgroundErrorCount;
		}

		// Reserves a block of ids inside the caller's write transaction.
		[[nodiscard]] std::optional<IdBlock> ReserveIdBlock(std::wstring& errorMessage)
		{
			const std::int64_t sequence = QueryInteger(database,
			    "SELECT seq FROM sqlite_sequence WHERE name = 'pitch_events';").value_or(0);
			const std::int64_t maximumId = QueryInteger(database, "SELECT MAX(id) FROM pitch_events;").value_or(0);
			const std::int64_t first = (std::max)(sequence, maximumId) + 1;
			const std::int64_t last = first + EventIdBlockSize - 1;
			const std::string update = "UPDATE sqlite_sequence SET seq = " + std::to_string(last) +
			    " WHERE name = 'pitch_events';";
			if (!Execute(database, update.c_str(), errorMessage))
			{
				return std::nullopt;
			}
			if (sqlite3_changes(database) == 0)
			{
				const std::string insert = "INSERT INTO sqlite_sequence (name, seq) VALUES ('pitch_events', " +
				    std::to_string(last) + ");";
				if (!Execute(database, insert.c_str(), errorMessage))
				{
					return std::nullopt;
				}
			}
			return IdBlock{ first, last + 1 };
		}

		// Returns ids that were reserved but never used, when no other connection reserved after us.
		void ReleaseUnusedIds()
		{
			std::wstring error{};
			if (latestReservation.end == 0 || !Execute(database, "BEGIN IMMEDIATE;", error))
			{
				return;
			}
			const std::int64_t sequence = QueryInteger(database,
			    "SELECT seq FROM sqlite_sequence WHERE name = 'pitch_events';").value_or(0);
			if (sequence == latestReservation.end - 1)
			{
				const std::int64_t maximumId = QueryInteger(database, "SELECT MAX(id) FROM pitch_events;").value_or(0);
				const std::int64_t lastIssued = issuedRanges.empty() ? 0 : issuedRanges.back().end - 1;
				const std::int64_t released = (std::max)({ maximumId, latestReservation.first - 1, lastIssued });
				const std::string update = "UPDATE sqlite_sequence SET seq = " + std::to_string(released) +
				    " WHERE name = 'pitch_events';";
				(void)Execute(database, update.c_str(), error);
			}
			(void)Execute(database, "COMMIT;", error);
		}

		[[nodiscard]] sqlite3_stmt* Statement(const char* sql)
		{
			const auto existing = statements.find(sql);
			if (existing != statements.end())
			{
				sqlite3_reset(existing->second);
				sqlite3_clear_bindings(existing->second);
				return existing->second;
			}
			sqlite3_stmt* statement = nullptr;
			if (sqlite3_prepare_v3(database, sql, -1, SQLITE_PREPARE_PERSISTENT, &statement, nullptr) != SQLITE_OK)
			{
				sqlite3_finalize(statement);
				return nullptr;
			}
			statements.emplace(sql, statement);
			return statement;
		}

		[[nodiscard]] bool Step(sqlite3_stmt* statement, const bool requireRow, std::wstring& errorMessage)
		{
			if (statement == nullptr)
			{
				errorMessage = L"Failed to prepare a session log statement: " + DescribeSqliteError(database);
				return false;
			}
			if (sqlite3_step(statement) != SQLITE_DONE)
			{
				errorMessage = DescribeSqliteError(database);
				sqlite3_reset(statement);
				return false;
			}
			sqlite3_reset(statement);
			if (requireRow && sqlite3_changes(database) == 0)
			{
				errorMessage = L"the referenced session log row does not exist";
				return false;
			}
			return true;
		}

		[[nodiscard]] bool Run(const InsertEventJob& job, std::wstring& errorMessage)
		{
			const EventRow& row = job.row;
			sqlite3_stmt* statement = Statement(InsertEventSql);
			const bool bound = statement != nullptr &&
			    sqlite3_bind_int64(statement, 1, row.id) == SQLITE_OK && BindText(statement, 2, row.createdAt) &&
			    BindText(statement, 3, sessionIdUtf8) && BindText(statement, 4, row.eventType) &&
			    BindText(statement, 5, row.category) && BindText(statement, 6, row.presetName) &&
			    BindText(statement, 7, row.integratorName) && sqlite3_bind_double(statement, 8, row.speed) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 9, row.spinRpm) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 10, row.windForward) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 11, row.windCross) == SQLITE_OK &&
			    sqlite3_bind_int(statement, 12, row.heightWind ? 1 : 0) == SQLITE_OK &&
			    sqlite3_bind_int(statement, 13, row.seam ? 1 : 0) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 14, row.seamAmplitude) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 15, row.seamFrequency) == SQLITE_OK && BindText(statement, 16, row.note);
			if (!bound)
			{
				errorMessage = L"Failed to bind a session event: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, false, errorMessage);
		}

		[[nodiscard]] bool Run(const InsertPitchJob& job, std::wstring& errorMessage)
		{
			sqlite3_stmt* statement = Statement(InsertPitchSql);
			const bool bound = statement != nullptr && sqlite3_bind_int64(statement, 1, job.eventId) == SQLITE_OK &&
			    BindText(statement, 2, sessionIdUtf8) && BindText(statement, 3, job.mode) &&
			    BindText(statement, 4, job.profileName) && BindText(statement, 5, job.pitchSettingsFile) &&
			    BindText(statement, 6, job.pitcherHand) && sqlite3_bind_double(statement, 7, job.releaseSpeed) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 8, job.spinRpm) == SQLITE_OK;
			if (!bound)
			{
				errorMessage = L"Failed to bind a pitch row: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, false, errorMessage);
		}

		[[nodiscard]] bool Run(const PitchOutcomeJob& job, std::wstring& errorMessage)
		{
			sqlite3_stmt* statement = Statement(UpdatePitchOutcomeSql);
			const bool bound = statement != nullptr && BindOptional(statement, 1, job.plateX) &&
			    BindOptional(statement, 2, job.plateY) && BindOptionalText(statement, 3, job.zoneCall) &&
			    BindOptional(statement, 4, job.horizontalBreak) && BindOptional(statement, 5, job.verticalBreak) &&
			    BindText(statement, 6, job.resolvedAt) && sqlite3_bind_int64(statement, 7, job.eventId) == SQLITE_OK;
			if (!bound)
			{
				errorMessage = L"Failed to bind a pitch outcome: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, true, errorMessage);
		}

		[[nodiscard]] bool Run(const InsertBattedBallJob& job, std::wstring& errorMessage)
		{
			sqlite3_stmt* statement = Statement(InsertBattedBallSql);
			const bool bound = statement != nullptr && sqlite3_bind_int64(statement, 1, job.eventId) == SQLITE_OK &&
			    (job.pitchEventId.has_value() ? sqlite3_bind_int64(statement, 2, *job.pitchEventId) == SQLITE_OK
			                                  : sqlite3_bind_null(statement, 2) == SQLITE_OK) &&
			    BindText(statement, 3, sessionIdUtf8) && BindText(statement, 4, job.mode) &&
			    sqlite3_bind_double(statement, 5, job.exitSpeed) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 6, job.launchAngle) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 7, job.sprayAngle) == SQLITE_OK &&
			    BindOptional(statement, 8, job.carryDistance) &&
			    sqlite3_bind_double(statement, 9, job.contactFraction) == SQLITE_OK &&
			    BindText(statement, 10, job.batMaterial) && BindText(statement, 11, job.batHand);
			if (!bound)
			{
				errorMessage = L"Failed to bind a batted-ball row: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, false, errorMessage);
		}

		[[nodiscard]] bool Run(const BattedBallLandingJob& job, std::wstring& errorMessage)
		{
			sqlite3_stmt* statement = Statement(UpdateBattedBallLandingSql);
			const bool bound = statement != nullptr && sqlite3_bind_double(statement, 1, job.carryDistance) == SQLITE_OK &&
			    sqlite3_bind_double(statement, 2, job.maxHeight) == SQLITE_OK &&
			    sqlite3_bind_int64(statement, 3, job.eventId) == SQLITE_OK;
			if (!bound)
			{
				errorMessage = L"Failed to bind a batted-ball landing: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, true, errorMessage);
		}

		[[nodiscard]] bool Run(const UpsertResultJob& job, std::wstring& errorMessage)
		{
			sqlite3_stmt* statement = Statement(UpsertResultSql);
			const bool bound = statement != nullptr && sqlite3_bind_int64(statement, 1, job.eventId) == SQLITE_OK &&
			    BindText(statement, 2, job.label) && sqlite3_bind_double(statement, 3, job.score) == SQLITE_OK &&
			    BindText(statement, 4, job.note) && BindText(statement, 5, job.updatedAt);
			if (!bound)
			{
				errorMessage = L"Failed to bind a pitch result: " + DescribeSqliteError(database);
				return false;
			}
			return Step(statement, false, errorMessage);
		}

		void ProcessBatch(std::vector<Job>& batch)
		{
			std::optional<IdBlock> reserved{};
			bool reservationAttempted = false;
			{
				const std::lock_guard databaseLock(databaseMutex);
				std::wstring error{};
				const bool transaction = Execute(database, "BEGIN IMMEDIATE;", error);
				if (!transaction)
				{
					RecordBackgroundError(L"Session log batch could not start a transaction: " + error);
				}
				for (const Job& job : batch)
				{
					if (std::holds_alternative<ReserveIdsJob>(job))
					{
						reservationAttempted = true;
						error.clear();
						if (transaction)
						{
							reserved = ReserveIdBlock(error);
						}
						if (!reserved)
						{
							RecordBackgroundError(L"Failed to reserve session event ids: " + error);
						}
						continue;
					}
					error.clear();
					const bool savepoint = transaction && Execute(database, "SAVEPOINT job;", error);
					const bool succeeded = std::visit(
					    [&](const auto& concrete) -> bool
					    {
						    if constexpr (std::is_same_v<std::decay_t<decltype(concrete)>, ReserveIdsJob>)
							    return true;
						    else
							    return Run(concrete, error);
					    },
					    job);
					std::wstring savepointError{};
					if (!succeeded)
					{
						RecordBackgroundError(L"Session log write failed: " + error);
						if (savepoint)
						{
							(void)Execute(database, "ROLLBACK TO job;", savepointError);
						}
					}
					if (savepoint)
					{
						(void)Execute(database, "RELEASE job;", savepointError);
					}
				}
				if (transaction && !Execute(database, "COMMIT;", error))
				{
					RecordBackgroundError(L"Session log batch could not be committed: " + error);
					std::wstring rollbackError{};
					(void)Execute(database, "ROLLBACK;", rollbackError);
					reserved.reset();
				}
			}

			const std::lock_guard lock(mutex);
			if (reservationAttempted)
			{
				reservationRequested = false;
				if (reserved)
				{
					pendingBlock = reserved;
					latestReservation = *reserved;
				}
			}
			completedJobs += batch.size();
			progress.notify_all();
		}

		void WorkerLoop()
		{
			for (;;)
			{
				std::vector<Job> batch{};
				{
					std::unique_lock lock(mutex);
					workAvailable.wait(lock, [&] { return stopRequested || !queue.empty(); });
					if (queue.empty())
					{
						break;
					}
					batch.swap(queue);
				}
				ProcessBatch(batch);
			}
			const std::lock_guard lock(mutex);
			workerRunning = false;
			progress.notify_all();
		}

		// Must be called with mutex held.
		void EnqueueLocked(Job job)
		{
			queue.push_back(std::move(job));
			++enqueuedJobs;
			workAvailable.notify_one();
		}

		// Must be called with the lock held; may wait for the writer when the id block is exhausted.
		[[nodiscard]] std::optional<std::int64_t> AssignEventIdLocked(std::unique_lock<std::mutex>& lock, std::wstring& errorMessage)
		{
			if (nextEventId >= blockEnd)
			{
				if (!pendingBlock)
				{
					if (!reservationRequested)
					{
						reservationRequested = true;
						EnqueueLocked(ReserveIdsJob{});
					}
					progress.wait(lock, [&] { return pendingBlock.has_value() || !reservationRequested || !workerRunning; });
				}
				if (!pendingBlock)
				{
					errorMessage = L"Failed to reserve session event ids.";
					return std::nullopt;
				}
				nextEventId = pendingBlock->first;
				blockEnd = pendingBlock->end;
				pendingBlock.reset();
				issuedRanges.push_back(IdBlock{ nextEventId, nextEventId });
			}
			const std::int64_t id = nextEventId++;
			issuedRanges.back().end = nextEventId;
			if (!pendingBlock && !reservationRequested && blockEnd - nextEventId < EventIdBlockSize / 4)
			{
				reservationRequested = true;
				EnqueueLocked(ReserveIdsJob{});
			}
			return id;
		}

		[[nodiscard]] bool IsIssuedLocked(const std::int64_t id) const noexcept
		{
			return std::any_of(issuedRanges.begin(), issuedRanges.end(),
			    [&](const IdBlock& range) { return id >= range.first && id < range.end; });
		}

		[[nodiscard]] bool EventExistsInDatabase(const std::int64_t id)
		{
			const std::lock_guard databaseLock(databaseMutex);
			const StatementPointer statement = Prepare(database, "SELECT 1 FROM pitch_events WHERE id = ?;");
			return statement && sqlite3_bind_int64(statement.get(), 1, id) == SQLITE_OK &&
			       sqlite3_step(statement.get()) == SQLITE_ROW;
		}

		[[nodiscard]] OperationResult Flush()
		{
			std::unique_lock lock(mutex);
			const std::uint64_t target = enqueuedJobs;
			progress.wait(lock, [&] { return completedJobs >= target || !workerRunning; });
			if (backgroundErrorCount == 0U)
			{
				return { true, {} };
			}
			std::wstring message = firstBackgroundError;
			if (backgroundErrorCount > 1U)
			{
				message += L" (+" + std::to_wstring(backgroundErrorCount - 1U) + L" more)";
			}
			firstBackgroundError.clear();
			backgroundErrorCount = 0U;
			return { false, std::move(message) };
		}

		void Close()
		{
			if (database == nullptr)
			{
				return;
			}
			if (worker.joinable())
			{
				const OperationResult flushed = Flush();
				if (!flushed)
				{
					Common::AppLog::Error(LogCategory, L"Pending session log writes failed: " + flushed.errorMessage);
				}
				{
					const std::lock_guard lock(mutex);
					stopRequested = true;
					workAvailable.notify_all();
				}
				worker.join();
			}
			for (auto& [sql, statement] : statements)
			{
				(void)sql;
				sqlite3_finalize(statement);
			}
			statements.clear();
			ReleaseUnusedIds();
			sqlite3_close(database);
			database = nullptr;
		}
	};

	SessionLogRepository::SessionLogRepository() = default;

	SessionLogRepository::~SessionLogRepository()
	{
		Shutdown();
	}

	OperationResult SessionLogRepository::Initialize(const std::wstring_view databasePath)
	{
		std::wstring errorMessage{};
		const bool succeeded = Initialize(databasePath, errorMessage);
		return OperationResult{ succeeded, errorMessage };
	}

	bool SessionLogRepository::Initialize(std::wstring_view databasePath, std::wstring& errorMessage)
	{
		errorMessage.clear();
		Shutdown();

		const std::optional<std::string> databasePathUtf8 = Text::TryWideToUtf8(databasePath);
		if (!databasePathUtf8.has_value() || databasePathUtf8->empty())
		{
			errorMessage = L"Failed to convert SQLite database path to UTF-8.";
			return false;
		}

		auto implementation = std::make_unique<Implementation>();
		sqlite3* database = nullptr;
		const int openResult = sqlite3_open_v2(databasePathUtf8->c_str(), &database,
		    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
		if (openResult != SQLITE_OK || database == nullptr)
		{
			errorMessage = L"Failed to open SQLite database: " +
			    (database != nullptr ? DescribeSqliteError(database) : std::wstring{ L"sqlite3_open_v2 returned null database" });
			sqlite3_close(database);
			return false;
		}
		implementation->database = database;
		sqlite3_busy_timeout(database, BusyTimeoutMilliseconds);

		std::wstring error{};
		{
			const StatementPointer journal = Prepare(database, "PRAGMA journal_mode = WAL;");
			const char* mode = nullptr;
			if (journal && sqlite3_step(journal.get()) == SQLITE_ROW)
			{
				mode = reinterpret_cast<const char*>(sqlite3_column_text(journal.get(), 0));
			}
			if (mode == nullptr || std::string_view{ mode } != "wal")
			{
				Common::AppLog::Warning(LogCategory, L"SQLite WAL journal mode is unavailable; using " +
				    Text::Utf8ToWideLossy(mode != nullptr ? mode : "the default journal"));
			}
		}
		if (!Execute(database, "PRAGMA synchronous = NORMAL; PRAGMA foreign_keys = ON;", error))
		{
			errorMessage = L"Failed to configure SQLite: " + error;
			return false;
		}

		// Schema migrations keyed by PRAGMA user_version.
		const std::int64_t version = QueryInteger(database, "PRAGMA user_version;").value_or(0);
		if (version > CurrentSchemaVersion)
		{
			errorMessage = L"The session database was created by a newer application version (schema " +
			    std::to_wstring(version) + L").";
			return false;
		}
		const auto migrate = [&](const auto& step, const std::wstring_view description) -> bool
		{
			if (!Execute(database, "BEGIN IMMEDIATE;", error))
			{
				errorMessage = std::wstring{ description } + L": " + error;
				return false;
			}
			if (!step())
			{
				errorMessage = std::wstring{ description } + L": " + error;
				std::wstring rollbackError{};
				(void)Execute(database, "ROLLBACK;", rollbackError);
				return false;
			}
			if (!Execute(database, "COMMIT;", error))
			{
				errorMessage = std::wstring{ description } + L": " + error;
				return false;
			}
			return true;
		};
		if (version < 1 &&
		    !migrate([&] {
			    return Execute(database, CreatePitchEventTableSql, error) &&
			           EnsureColumnExists(database, "pitch_events", "session_id", "session_id TEXT NOT NULL DEFAULT ''", error) &&
			           Execute(database, CreatePitchEventIndexesSql, error) &&
			           Execute(database, CreatePitchEventResultsTableSql, error) &&
			           Execute(database, "PRAGMA user_version = 1;", error);
		    }, L"Failed to create the session log schema"))
		{
			return false;
		}
		if (version < 2 &&
		    !migrate([&] {
			    return EnsureColumnExists(database, "pitch_events", "event_category",
			               "event_category TEXT NOT NULL DEFAULT 'config'", error) &&
			           Execute(database, BackfillEventCategorySql, error) &&
			           Execute(database, CreateNormalizedTablesSql, error) &&
			           Execute(database, RecreateViewsSql, error) &&
			           Execute(database, "PRAGMA user_version = 2;", error);
		    }, L"Failed to migrate the session log schema to version 2"))
		{
			return false;
		}

		implementation->sessionId = BuildSessionIdentifier();
		implementation->sessionIdUtf8 = Text::TryWideToUtf8(implementation->sessionId).value_or(std::string{});
		std::optional<IdBlock> firstBlock{};
		if (!migrate([&] {
			    firstBlock = implementation->ReserveIdBlock(error);
			    return firstBlock.has_value();
		    }, L"Failed to reserve session event ids"))
		{
			return false;
		}
		implementation->nextEventId = firstBlock->first;
		implementation->blockEnd = firstBlock->end;
		implementation->latestReservation = *firstBlock;
		implementation->issuedRanges.push_back(IdBlock{ firstBlock->first, firstBlock->first });

		implementation->workerRunning = true;
		Implementation* const worker = implementation.get();
		implementation->worker = std::thread([worker] { worker->WorkerLoop(); });
		m_implementation = std::move(implementation);
		return true;
	}

	void SessionLogRepository::Shutdown()
	{
		m_implementation.reset();
	}

	bool SessionLogRepository::IsInitialized() const noexcept
	{
		return m_implementation != nullptr && m_implementation->IsOpen();
	}

	OperationResult SessionLogRepository::Flush()
	{
		if (!IsInitialized())
		{
			return { false, L"SQLite database is not initialized." };
		}
		return m_implementation->Flush();
	}

	std::wstring SessionLogRepository::GetSessionId() const
	{
		return IsInitialized() ? m_implementation->sessionId : std::wstring{};
	}

	OperationResult SessionLogRepository::ExportCurrentSessionCsv(const std::filesystem::path& filePath)
	{
		if (!IsInitialized())
		{
			return { false, L"SQLite database is not initialized." };
		}
		if (filePath.empty() || filePath.filename().empty())
		{
			return { false, L"CSV output file path is empty." };
		}
		const OperationResult flushed = m_implementation->Flush();
		if (!flushed)
		{
			Common::AppLog::Error(LogCategory, L"Some session log writes failed before export: " + flushed.errorMessage);
		}

		Implementation& implementation = *m_implementation;
		const std::lock_guard databaseLock(implementation.databaseMutex);
		sqlite3* const database = implementation.database;
		const char* databaseName = sqlite3_db_filename(database, "main");
		if (databaseName != nullptr && *databaseName != '\0')
		{
			std::error_code error;
			if (std::filesystem::equivalent(filePath, std::filesystem::path(Text::Utf8ToWideLossy(databaseName)), error))
			{
				return { false, L"CSV output must not replace the session database." };
			}
		}

		const StatementPointer statement = Prepare(database,
		    "SELECT * FROM pitch_event_comparison_with_results WHERE session_id = ? ORDER BY id;");
		if (!statement)
		{
			return { false, L"Failed to prepare CSV query: " + DescribeSqliteError(database) };
		}
		if (!BindText(statement.get(), 1, implementation.sessionIdUtf8))
		{
			return { false, L"Failed to bind CSV session: " + DescribeSqliteError(database) };
		}

		// Write into a unique sibling first so a failed export never replaces a completed one.
		Common::AtomicFileWriter output{ filePath };
		if (const Common::FileOperationResult opened = output.Open(); !opened)
		{
			return { false, L"Failed to create CSV output file: " + opened.errorMessage };
		}
		const auto appendQuoted = [](std::string& row, const std::string_view value)
		{
			row += '"';
			for (const char character : value)
			{
				if (character == '"')
				{
					row += '"';
				}
				row += character;
			}
			row += '"';
		};
		std::string row = "\xEF\xBB\xBF";
		const int columnCount = sqlite3_column_count(statement.get());
		for (int column = 0; column < columnCount; ++column)
		{
			if (column != 0)
				row += ',';
			appendQuoted(row, sqlite3_column_name(statement.get(), column));
		}
		row += "\r\n";
		if (!output.Write(row))
		{
			return { false, L"Failed to write CSV header." };
		}
		int stepResult = SQLITE_OK;
		while ((stepResult = sqlite3_step(statement.get())) == SQLITE_ROW)
		{
			row.clear();
			for (int column = 0; column < columnCount; ++column)
			{
				if (column != 0)
					row += ',';
				const int type = sqlite3_column_type(statement.get(), column);
				if (type == SQLITE_NULL)
					continue;
				const auto* value = sqlite3_column_text(statement.get(), column);
				if (value == nullptr)
				{
					return { false, L"Failed to read CSV column." };
				}
				const std::string_view text(reinterpret_cast<const char*>(value),
				    static_cast<std::size_t>(sqlite3_column_bytes(statement.get(), column)));
				if (type == SQLITE_TEXT)
					appendQuoted(row, text);
				else
					row += text;
			}
			row += "\r\n";
			if (!output.Write(row))
			{
				return { false, L"Failed to write CSV row." };
			}
		}
		if (stepResult != SQLITE_DONE)
		{
			return { false, L"Failed to query session for CSV: " + DescribeSqliteError(database) };
		}
		if (const Common::FileOperationResult committed = output.Commit(); !committed)
		{
			return { false, L"Failed to publish CSV output: " + committed.errorMessage };
		}
		return { true, {} };
	}

	PitchEventLogResult SessionLogRepository::LogPitchEvent(
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note)
	{
		return LogEvent(ClassifySessionEventType(eventType, note), eventType, presetName, integratorName,
		    pitchParameters, note);
	}

	std::optional<std::int64_t> SessionLogRepository::LogPitchEvent(
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note,
	    std::wstring& errorMessage)
	{
		PitchEventLogResult result = LogPitchEvent(eventType, presetName, integratorName, pitchParameters, note);
		errorMessage = std::move(result.errorMessage);
		return result.eventId;
	}

	namespace
	{
		[[nodiscard]] std::optional<EventRow> BuildEventRow(
		    const SessionEventCategory category,
		    const std::wstring_view eventType,
		    const std::wstring_view presetName,
		    const std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    const std::wstring_view note,
		    std::wstring& errorMessage)
		{
			EventRow row{};
			row.speed = Physics::Length(pitchParameters.initialVelocity);
			row.spinRpm = pitchParameters.spinRpm;
			row.windForward = pitchParameters.windVelocity.z;
			row.windCross = pitchParameters.windVelocity.x;
			row.heightWind = pitchParameters.enableHeightDependentWind;
			row.seam = pitchParameters.enableSeamPerturbation;
			row.seamAmplitude = pitchParameters.seamPerturbationAmplitude;
			row.seamFrequency = pitchParameters.seamPerturbationFrequencyHz;
			if (!std::isfinite(row.speed) || !std::isfinite(row.spinRpm) || !std::isfinite(row.windForward) ||
			    !std::isfinite(row.windCross) || !std::isfinite(row.seamAmplitude) || !std::isfinite(row.seamFrequency))
			{
				errorMessage = L"Pitch event numeric values must be finite.";
				return std::nullopt;
			}
			const auto eventTypeUtf8 = Text::TryWideToUtf8(eventType);
			const auto presetNameUtf8 = Text::TryWideToUtf8(presetName);
			const auto integratorNameUtf8 = Text::TryWideToUtf8(integratorName);
			const auto noteUtf8 = Text::TryWideToUtf8(note);
			const auto categoryUtf8 = Text::TryWideToUtf8(GetSessionEventCategoryToken(category));
			if (!eventTypeUtf8 || !presetNameUtf8 || !integratorNameUtf8 || !noteUtf8 || !categoryUtf8)
			{
				errorMessage = L"Pitch event text is not valid Unicode.";
				return std::nullopt;
			}
			row.eventType = *eventTypeUtf8;
			row.presetName = *presetNameUtf8;
			row.integratorName = *integratorNameUtf8;
			row.note = *noteUtf8;
			row.category = *categoryUtf8;
			row.createdAt = BuildUtcTimestamp();
			return row;
		}
	} // namespace

	PitchEventLogResult SessionLogRepository::LogEvent(
	    const SessionEventCategory category,
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note)
	{
		if (!IsInitialized())
		{
			return { std::nullopt, L"SQLite database is not initialized." };
		}
		std::wstring error{};
		std::optional<EventRow> row =
		    BuildEventRow(category, eventType, presetName, integratorName, pitchParameters, note, error);
		if (!row)
		{
			return { std::nullopt, error };
		}
		Implementation& implementation = *m_implementation;
		std::unique_lock lock(implementation.mutex);
		const std::optional<std::int64_t> id = implementation.AssignEventIdLocked(lock, error);
		if (!id)
		{
			return { std::nullopt, error };
		}
		row->id = *id;
		implementation.EnqueueLocked(InsertEventJob{ std::move(*row) });
		return { id, {} };
	}

	PitchEventLogResult SessionLogRepository::LogPitch(
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note,
	    const PitchDetails& details)
	{
		if (!IsInitialized())
		{
			return { std::nullopt, L"SQLite database is not initialized." };
		}
		std::wstring error{};
		std::optional<EventRow> row = BuildEventRow(
		    SessionEventCategory::Pitch, eventType, presetName, integratorName, pitchParameters, note, error);
		if (!row)
		{
			return { std::nullopt, error };
		}
		const auto mode = Text::TryWideToUtf8(details.mode);
		const auto profile = Text::TryWideToUtf8(details.profileName);
		const auto file = Text::TryWideToUtf8(details.pitchSettingsFile);
		if (!mode || !profile || !file)
		{
			return { std::nullopt, L"Pitch details are not valid Unicode." };
		}
		InsertPitchJob pitch{ 0, *mode, *profile, *file, details.pitcherThrowsLeftHanded ? "L" : "R", row->speed,
			row->spinRpm };
		Implementation& implementation = *m_implementation;
		std::unique_lock lock(implementation.mutex);
		const std::optional<std::int64_t> id = implementation.AssignEventIdLocked(lock, error);
		if (!id)
		{
			return { std::nullopt, error };
		}
		row->id = *id;
		pitch.eventId = *id;
		implementation.EnqueueLocked(InsertEventJob{ std::move(*row) });
		implementation.EnqueueLocked(std::move(pitch));
		return { id, {} };
	}

	OperationResult SessionLogRepository::RecordPitchOutcome(const std::int64_t pitchEventId, const PitchOutcome& outcome)
	{
		if (!IsInitialized())
		{
			return { false, L"SQLite database is not initialized." };
		}
		if (!IsFinite(outcome.plateCrossingX) || !IsFinite(outcome.plateCrossingY) ||
		    !IsFinite(outcome.horizontalBreakMeters) || !IsFinite(outcome.verticalBreakMeters))
		{
			return { false, L"Pitch outcome values must be finite." };
		}
		const auto call = Text::TryWideToUtf8(outcome.zoneCall);
		if (!call)
		{
			return { false, L"Pitch outcome call is not valid Unicode." };
		}
		Implementation& implementation = *m_implementation;
		const std::lock_guard lock(implementation.mutex);
		if (!implementation.IsIssuedLocked(pitchEventId))
		{
			return { false, L"Pitch outcomes can only be recorded for pitches of the current session." };
		}
		implementation.EnqueueLocked(PitchOutcomeJob{ pitchEventId, outcome.plateCrossingX, outcome.plateCrossingY, *call,
			outcome.horizontalBreakMeters, outcome.verticalBreakMeters, BuildUtcTimestamp() });
		return { true, {} };
	}

	PitchEventLogResult SessionLogRepository::LogBattedBall(
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note,
	    const BattedBallDetails& details)
	{
		if (!IsInitialized())
		{
			return { std::nullopt, L"SQLite database is not initialized." };
		}
		if (!std::isfinite(details.exitSpeedMetersPerSecond) || !std::isfinite(details.launchAngleDegrees) ||
		    !std::isfinite(details.sprayAngleDegrees) || !IsFinite(details.carryDistanceMeters) ||
		    !std::isfinite(details.contactFractionAlongBat) || details.contactFractionAlongBat < 0.0 ||
		    details.contactFractionAlongBat > 1.0 || details.exitSpeedMetersPerSecond < 0.0)
		{
			return { std::nullopt, L"Batted-ball values must be finite, with a contact fraction between 0 and 1." };
		}
		std::wstring error{};
		std::optional<EventRow> row = BuildEventRow(SessionEventCategory::BattedBall, L"bat_contact", presetName,
		    integratorName, pitchParameters, note, error);
		if (!row)
		{
			return { std::nullopt, error };
		}
		const auto mode = Text::TryWideToUtf8(details.mode);
		const auto material = Text::TryWideToUtf8(details.batMaterial);
		if (!mode || !material)
		{
			return { std::nullopt, L"Batted-ball details are not valid Unicode." };
		}
		InsertBattedBallJob batted{ 0, details.pitchEventId, *mode, details.exitSpeedMetersPerSecond,
			details.launchAngleDegrees, details.sprayAngleDegrees, details.carryDistanceMeters,
			details.contactFractionAlongBat, *material, details.batRightHanded ? "R" : "L" };
		Implementation& implementation = *m_implementation;
		std::unique_lock lock(implementation.mutex);
		if (details.pitchEventId.has_value() && !implementation.IsIssuedLocked(*details.pitchEventId))
		{
			batted.pitchEventId.reset();
		}
		const std::optional<std::int64_t> id = implementation.AssignEventIdLocked(lock, error);
		if (!id)
		{
			return { std::nullopt, error };
		}
		row->id = *id;
		batted.eventId = *id;
		implementation.EnqueueLocked(InsertEventJob{ std::move(*row) });
		implementation.EnqueueLocked(std::move(batted));
		return { id, {} };
	}

	OperationResult SessionLogRepository::UpdateBattedBallLanding(
	    const std::int64_t battedBallEventId, const double carryDistanceMeters, const double maxHeightMeters)
	{
		if (!IsInitialized())
		{
			return { false, L"SQLite database is not initialized." };
		}
		if (!std::isfinite(carryDistanceMeters) || !std::isfinite(maxHeightMeters))
		{
			return { false, L"Batted-ball landing values must be finite." };
		}
		Implementation& implementation = *m_implementation;
		const std::lock_guard lock(implementation.mutex);
		if (!implementation.IsIssuedLocked(battedBallEventId))
		{
			return { false, L"Landings can only be recorded for batted balls of the current session." };
		}
		implementation.EnqueueLocked(BattedBallLandingJob{ battedBallEventId, carryDistanceMeters, maxHeightMeters });
		return { true, {} };
	}

	OperationResult SessionLogRepository::UpsertPitchEventResult(
	    const std::int64_t eventId,
	    std::wstring_view resultLabel,
	    const double resultScore,
	    std::wstring_view note)
	{
		std::wstring errorMessage{};
		const bool succeeded = UpsertPitchEventResult(eventId, resultLabel, resultScore, note, errorMessage);
		return OperationResult{ succeeded, errorMessage };
	}

	bool SessionLogRepository::UpsertPitchEventResult(
	    const std::int64_t eventId,
	    std::wstring_view resultLabel,
	    const double resultScore,
	    std::wstring_view note,
	    std::wstring& errorMessage)
	{
		errorMessage.clear();
		if (!IsInitialized())
		{
			errorMessage = L"SQLite database is not initialized.";
			return false;
		}
		if (eventId <= 0)
		{
			errorMessage = L"SQLite result tag event ID must be positive.";
			return false;
		}
		if (!std::isfinite(resultScore))
		{
			errorMessage = L"Pitch result score must be finite.";
			return false;
		}
		const auto label = Text::TryWideToUtf8(resultLabel);
		const auto noteUtf8 = Text::TryWideToUtf8(note);
		if (!label || label->empty())
		{
			errorMessage = L"Failed to convert SQLite result label to UTF-8.";
			return false;
		}
		if (!noteUtf8)
		{
			errorMessage = L"Pitch result note is not valid Unicode.";
			return false;
		}

		Implementation& implementation = *m_implementation;
		bool known = false;
		{
			const std::lock_guard lock(implementation.mutex);
			known = implementation.IsIssuedLocked(eventId);
		}
		// Results for events of earlier sessions are checked against the committed rows.
		if (!known && !implementation.EventExistsInDatabase(eventId))
		{
			errorMessage = L"No session log event with ID " + std::to_wstring(eventId) + L" exists.";
			return false;
		}
		const std::lock_guard lock(implementation.mutex);
		implementation.EnqueueLocked(UpsertResultJob{ eventId, *label, resultScore, *noteUtf8, BuildUtcTimestamp() });
		return true;
	}
} // namespace BaseballSimulatorVR::Data

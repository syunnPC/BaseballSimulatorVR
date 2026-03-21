#include "SessionLogRepository.hpp"

#if defined(_WIN64)
#include <sqlite3.h>
#endif

#include "../Framework.hpp"
#include "../Physics/Vector3.hpp"
#include "../Physics/PitchIntegratorType.hpp"

#include <cctype>
#include <cmath>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
#if defined(_WIN64)
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

		constexpr char CreatePitchTrialSummaryViewSql[] =
		    "CREATE VIEW IF NOT EXISTS pitch_trial_summary AS "
		    "SELECT "
		    " session_id,"
		    " preset_name,"
		    " integrator_name,"
		    " COUNT(*) AS event_count,"
		    " MIN(created_at) AS first_event_at,"
		    " MAX(created_at) AS last_event_at,"
		    " AVG(speed) AS avg_speed,"
		    " AVG(spin_rpm) AS avg_spin_rpm,"
		    " AVG(wind_forward) AS avg_wind_forward,"
		    " AVG(wind_cross) AS avg_wind_cross,"
		    " SUM(CASE WHEN height_wind_enabled <> 0 THEN 1 ELSE 0 END) AS height_wind_enabled_count,"
		    " SUM(CASE WHEN seam_enabled <> 0 THEN 1 ELSE 0 END) AS seam_enabled_count "
		    "FROM pitch_events "
		    "GROUP BY session_id, preset_name, integrator_name;";

		constexpr char CreatePitchPresetComparisonViewSql[] =
		    "CREATE VIEW IF NOT EXISTS pitch_preset_comparison AS "
		    "SELECT "
		    " preset_name,"
		    " event_type,"
		    " COUNT(*) AS sample_count,"
		    " COUNT(DISTINCT session_id) AS session_count,"
		    " AVG(speed) AS avg_speed,"
		    " AVG(spin_rpm) AS avg_spin_rpm,"
		    " AVG(wind_forward) AS avg_wind_forward,"
		    " AVG(wind_cross) AS avg_wind_cross,"
		    " AVG(seam_amp) AS avg_seam_amp,"
		    " AVG(seam_freq) AS avg_seam_freq "
		    "FROM pitch_events "
		    "GROUP BY preset_name, event_type;";

		constexpr char CreatePitchEventResultsTableSql[] =
		    "CREATE TABLE IF NOT EXISTS pitch_event_results ("
		    " event_id INTEGER PRIMARY KEY,"
		    " result_label TEXT NOT NULL,"
		    " result_score REAL NOT NULL,"
		    " note TEXT NOT NULL,"
		    " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
		    " FOREIGN KEY(event_id) REFERENCES pitch_events(id) ON DELETE CASCADE"
		    ");";

		constexpr char CreatePitchEventResultsIndexesSql[] =
		    "CREATE INDEX IF NOT EXISTS idx_pitch_event_results_label ON pitch_event_results(result_label);"
		    "CREATE INDEX IF NOT EXISTS idx_pitch_event_results_score ON pitch_event_results(result_score);";

		constexpr char CreatePitchEventComparisonWithResultsViewSql[] =
		    "CREATE VIEW IF NOT EXISTS pitch_event_comparison_with_results AS "
		    "SELECT "
		    " e.id,"
		    " e.created_at,"
		    " e.session_id,"
		    " e.event_type,"
		    " e.preset_name,"
		    " e.integrator_name,"
		    " e.speed,"
		    " e.spin_rpm,"
		    " e.wind_forward,"
		    " e.wind_cross,"
		    " e.height_wind_enabled,"
		    " e.seam_enabled,"
		    " e.seam_amp,"
		    " e.seam_freq,"
		    " e.note AS event_note,"
		    " r.result_label,"
		    " r.result_score,"
		    " r.note AS result_note,"
		    " r.updated_at AS result_updated_at "
		    "FROM pitch_events e "
		    "LEFT JOIN pitch_event_results r ON r.event_id = e.id;";

		constexpr char SessionIdColumnDefinition[] = "session_id TEXT NOT NULL DEFAULT ''";

		[[nodiscard]] bool IsSqlIdentifier(const std::string_view identifier)
		{
			if (identifier.empty())
			{
				return false;
			}

			for (const char character : identifier)
			{
				if ((character == '_') ||
				    std::isalnum(static_cast<unsigned char>(character)))
				{
					continue;
				}
				return false;
			}
			return true;
		}

		[[nodiscard]] bool IsValidColumnDefinition(
		    const std::string_view columnName,
		    const std::string_view columnDefinition)
		{
			if (!IsSqlIdentifier(columnName) || columnDefinition.empty())
			{
				return false;
			}

			const std::size_t firstWhitespace =
			    columnDefinition.find_first_of(" \t\r\n");
			if (firstWhitespace == std::string_view::npos)
			{
				return false;
			}
			return columnDefinition.substr(0, firstWhitespace) == columnName;
		}

		[[nodiscard]] std::wstring BuildSessionIdentifier()
		{
			FILETIME fileTime{};
			GetSystemTimeAsFileTime(&fileTime);
			ULARGE_INTEGER timestamp{};
			timestamp.LowPart = fileTime.dwLowDateTime;
			timestamp.HighPart = fileTime.dwHighDateTime;
			return L"pid" + std::to_wstring(GetCurrentProcessId()) +
			       L"-" + std::to_wstring(timestamp.QuadPart);
		}
#endif
	} // namespace

	SessionLogRepository::~SessionLogRepository()
	{
		Shutdown();
	}

	OperationResult SessionLogRepository::Initialize(
	    const std::wstring_view databasePath)
	{
		std::wstring errorMessage{};
		const bool succeeded = Initialize(databasePath, errorMessage);
		return OperationResult{
			succeeded,
			errorMessage
		};
	}

	bool SessionLogRepository::Initialize(std::wstring_view databasePath, std::wstring& errorMessage)
	{
		errorMessage.clear();

		Shutdown();

#if !defined(_WIN64)
		errorMessage = L"SQLite logging is only enabled for x64 builds.";
		return false;
#else
		const std::string databasePathUtf8 = ToUtf8(databasePath);
		if (databasePathUtf8.empty())
		{
			errorMessage = L"Failed to convert SQLite database path to UTF-8.";
			return false;
		}

		sqlite3* database = nullptr;
		const int openResult = sqlite3_open_v2(
		    databasePathUtf8.c_str(),
		    &database,
		    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
		    nullptr);
		if ((openResult != SQLITE_OK) || (database == nullptr))
		{
			const char* sqliteErrorText = (database != nullptr) ? sqlite3_errmsg(database) : "sqlite3_open_v2 returned null database";
			errorMessage = L"Failed to open SQLite database: " + ToWide(sqliteErrorText);
			if (database != nullptr)
			{
				sqlite3_close(database);
			}
			return false;
		}

		m_database = database;
		if (!EnsureSchema(errorMessage))
		{
			Shutdown();
			return false;
		}

		m_sessionId = BuildSessionIdentifier();
		return true;
#endif
	}

	void SessionLogRepository::Shutdown()
	{
#if defined(_WIN64)
		if (m_database != nullptr)
		{
			sqlite3_close(m_database);
			m_database = nullptr;
		}
#else
		m_database = nullptr;
#endif
		m_sessionId.clear();
	}

	bool SessionLogRepository::IsInitialized() const noexcept
	{
		return m_database != nullptr;
	}

	PitchEventLogResult SessionLogRepository::LogPitchEvent(
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note)
	{
		std::wstring errorMessage{};
		std::optional<std::int64_t> eventId = LogPitchEvent(
		    eventType,
		    presetName,
		    integratorName,
		    pitchParameters,
		    note,
		    errorMessage);
		return PitchEventLogResult{
			eventId,
			errorMessage
		};
	}

	std::optional<std::int64_t> SessionLogRepository::LogPitchEvent(
	    std::wstring_view eventType,
	    std::wstring_view presetName,
	    std::wstring_view integratorName,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring_view note,
	    std::wstring& errorMessage)
	{
		errorMessage.clear();

#if !defined(_WIN64)
		errorMessage = L"SQLite logging is only enabled for x64 builds.";
		return std::nullopt;
#else
		if (m_database == nullptr)
		{
			errorMessage = L"SQLite database is not initialized.";
			return std::nullopt;
		}

		constexpr char InsertPitchEventSql[] =
		    "INSERT INTO pitch_events ("
		    " session_id, event_type, preset_name, integrator_name, speed, spin_rpm, wind_forward, wind_cross,"
		    " height_wind_enabled, seam_enabled, seam_amp, seam_freq, note)"
		    " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

		sqlite3_stmt* statement = nullptr;
		const int prepareResult = sqlite3_prepare_v2(m_database, InsertPitchEventSql, -1, &statement, nullptr);
		if ((prepareResult != SQLITE_OK) || (statement == nullptr))
		{
			errorMessage = L"Failed to prepare SQLite insert statement: " + ToWide(sqlite3_errmsg(m_database));
			if (statement != nullptr)
			{
				const int finalizeResult = sqlite3_finalize(statement);
				if (finalizeResult != SQLITE_OK)
				{
					errorMessage += L" (failed to finalize prepared statement)";
				}
			}
			return std::nullopt;
		}

		if (m_sessionId.empty())
		{
			m_sessionId = BuildSessionIdentifier();
		}

		const std::string sessionIdUtf8 = ToUtf8(m_sessionId);
		if (sessionIdUtf8.empty())
		{
			errorMessage = L"Failed to convert SQLite session ID to UTF-8.";
			const int finalizeResult = sqlite3_finalize(statement);
			if (finalizeResult != SQLITE_OK)
			{
				errorMessage += L" (failed to finalize prepared statement)";
			}
			return std::nullopt;
		}

		const std::string eventTypeUtf8 = ToUtf8(eventType);
		const std::string presetNameUtf8 = ToUtf8(presetName);
		const std::string integratorNameUtf8 = ToUtf8(integratorName);
		const std::string noteUtf8 = ToUtf8(note);

		const double speedMetersPerSecond =
		    Physics::Length(pitchParameters.initialVelocity);

		sqlite3_bind_text(statement, 1, sessionIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(statement, 2, eventTypeUtf8.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(statement, 3, presetNameUtf8.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(statement, 4, integratorNameUtf8.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_double(statement, 5, speedMetersPerSecond);
		sqlite3_bind_double(statement, 6, pitchParameters.spinRpm);
		sqlite3_bind_double(statement, 7, pitchParameters.windVelocity.z);
		sqlite3_bind_double(statement, 8, pitchParameters.windVelocity.x);
		sqlite3_bind_int(statement, 9, pitchParameters.enableHeightDependentWind ? 1 : 0);
		sqlite3_bind_int(statement, 10, pitchParameters.enableSeamPerturbation ? 1 : 0);
		sqlite3_bind_double(statement, 11, pitchParameters.seamPerturbationAmplitude);
		sqlite3_bind_double(statement, 12, pitchParameters.seamPerturbationFrequencyHz);
		sqlite3_bind_text(statement, 13, noteUtf8.c_str(), -1, SQLITE_TRANSIENT);

		const int stepResult = sqlite3_step(statement);
		const int finalizeResult = sqlite3_finalize(statement);
		if ((stepResult != SQLITE_DONE) || (finalizeResult != SQLITE_OK))
		{
			errorMessage = L"Failed to insert SQLite pitch event: " + ToWide(sqlite3_errmsg(m_database));
			return std::nullopt;
		}

		return static_cast<std::int64_t>(sqlite3_last_insert_rowid(m_database));
#endif
	}

	OperationResult SessionLogRepository::UpsertPitchEventResult(
	    const std::int64_t eventId,
	    std::wstring_view resultLabel,
	    const double resultScore,
	    std::wstring_view note)
	{
		std::wstring errorMessage{};
		const bool succeeded = UpsertPitchEventResult(
		    eventId,
		    resultLabel,
		    resultScore,
		    note,
		    errorMessage);
		return OperationResult{
			succeeded,
			errorMessage
		};
	}

	bool SessionLogRepository::UpsertPitchEventResult(
	    const std::int64_t eventId,
	    std::wstring_view resultLabel,
	    const double resultScore,
	    std::wstring_view note,
	    std::wstring& errorMessage)
	{
		errorMessage.clear();

#if !defined(_WIN64)
		(void)eventId;
		(void)resultLabel;
		(void)resultScore;
		(void)note;
		errorMessage = L"SQLite logging is only enabled for x64 builds.";
		return false;
#else
		if (m_database == nullptr)
		{
			errorMessage = L"SQLite database is not initialized.";
			return false;
		}

		if (eventId <= 0)
		{
			errorMessage = L"SQLite result tag event ID must be positive.";
			return false;
		}

		const std::string resultLabelUtf8 = ToUtf8(resultLabel);
		if (resultLabelUtf8.empty())
		{
			errorMessage = L"Failed to convert SQLite result label to UTF-8.";
			return false;
		}
		const std::string noteUtf8 = ToUtf8(note);

		constexpr char UpsertPitchEventResultSql[] =
		    "INSERT INTO pitch_event_results (event_id, result_label, result_score, note)"
		    " VALUES (?, ?, ?, ?)"
		    " ON CONFLICT(event_id) DO UPDATE SET"
		    " result_label = excluded.result_label,"
		    " result_score = excluded.result_score,"
		    " note = excluded.note,"
		    " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now');";

		sqlite3_stmt* statement = nullptr;
		const int prepareResult = sqlite3_prepare_v2(
		    m_database,
		    UpsertPitchEventResultSql,
		    -1,
		    &statement,
		    nullptr);
		if ((prepareResult != SQLITE_OK) || (statement == nullptr))
		{
			errorMessage = L"Failed to prepare SQLite result upsert statement: " +
			               ToWide(sqlite3_errmsg(m_database));
			if (statement != nullptr)
			{
				const int finalizeResult = sqlite3_finalize(statement);
				if (finalizeResult != SQLITE_OK)
				{
					errorMessage += L" (failed to finalize prepared statement)";
				}
			}
			return false;
		}

		sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(eventId));
		sqlite3_bind_text(statement, 2, resultLabelUtf8.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_double(statement, 3, resultScore);
		sqlite3_bind_text(statement, 4, noteUtf8.c_str(), -1, SQLITE_TRANSIENT);

		const int stepResult = sqlite3_step(statement);
		const int finalizeResult = sqlite3_finalize(statement);
		if ((stepResult != SQLITE_DONE) || (finalizeResult != SQLITE_OK))
		{
			errorMessage = L"Failed to upsert SQLite pitch result: " +
			               ToWide(sqlite3_errmsg(m_database));
			return false;
		}

		return true;
#endif
	}

	bool SessionLogRepository::EnsureSchema(std::wstring& errorMessage)
	{
#if !defined(_WIN64)
		errorMessage.clear();
		return false;
#else
		if (m_database == nullptr)
		{
			errorMessage = L"SQLite database is not initialized.";
			return false;
		}

		auto executeSchemaSql = [&](const char* sql, std::wstring_view context) -> bool
		{
			char* sqliteErrorMessage = nullptr;
			const int execResult = sqlite3_exec(m_database, sql, nullptr, nullptr, &sqliteErrorMessage);
			if (execResult == SQLITE_OK)
			{
				return true;
			}

			const std::string rawError = (sqliteErrorMessage != nullptr)
			                                 ? sqliteErrorMessage
			                                 : "unknown sqlite schema error";
			errorMessage = std::wstring{ context } + L": " + ToWide(rawError);
			if (sqliteErrorMessage != nullptr)
			{
				sqlite3_free(sqliteErrorMessage);
			}
			return false;
		};

		if (!executeSchemaSql(CreatePitchEventTableSql, L"Failed to create pitch_events table"))
		{
			return false;
		}

		if (!EnsureColumnExists(
		        "pitch_events",
		        "session_id",
		        SessionIdColumnDefinition,
		        errorMessage))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchEventIndexesSql, L"Failed to create pitch_events indexes"))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchTrialSummaryViewSql, L"Failed to create pitch_trial_summary view"))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchPresetComparisonViewSql, L"Failed to create pitch_preset_comparison view"))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchEventResultsTableSql, L"Failed to create pitch_event_results table"))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchEventResultsIndexesSql, L"Failed to create pitch_event_results indexes"))
		{
			return false;
		}

		if (!executeSchemaSql(CreatePitchEventComparisonWithResultsViewSql, L"Failed to create pitch_event_comparison_with_results view"))
		{
			return false;
		}

		return true;
#endif
	}

	bool SessionLogRepository::EnsureColumnExists(
	    std::string_view tableName,
	    std::string_view columnName,
	    std::string_view columnDefinition,
	    std::wstring& errorMessage)
	{
#if !defined(_WIN64)
		(void)tableName;
		(void)columnName;
		(void)columnDefinition;
		errorMessage.clear();
		return false;
#else
		if (m_database == nullptr)
		{
			errorMessage = L"SQLite database is not initialized.";
			return false;
		}

		const std::string tableNameText{ tableName };
		const std::string columnNameText{ columnName };
		const std::string columnDefinitionText{ columnDefinition };
		if (!IsSqlIdentifier(tableNameText) ||
		    !IsValidColumnDefinition(columnNameText, columnDefinitionText))
		{
			errorMessage = L"Invalid SQLite schema migration arguments.";
			return false;
		}

		const std::string pragmaSql =
		    "PRAGMA table_info(\"" + tableNameText + "\");";
		sqlite3_stmt* statement = nullptr;
		const int prepareResult =
		    sqlite3_prepare_v2(m_database, pragmaSql.c_str(), -1, &statement, nullptr);
		if ((prepareResult != SQLITE_OK) || (statement == nullptr))
		{
			errorMessage = L"Failed to inspect SQLite table info: " +
			               ToWide(sqlite3_errmsg(m_database));
			if (statement != nullptr)
			{
				const int finalizeResult = sqlite3_finalize(statement);
				if (finalizeResult != SQLITE_OK)
				{
					errorMessage += L" (failed to finalize table info statement)";
				}
			}
			return false;
		}

		bool columnExists = false;
		int stepResult = SQLITE_ROW;
		while ((stepResult = sqlite3_step(statement)) == SQLITE_ROW)
		{
			const unsigned char* rawColumnName = sqlite3_column_text(statement, 1);
			if ((rawColumnName != nullptr) &&
			    (columnNameText == reinterpret_cast<const char*>(rawColumnName)))
			{
				columnExists = true;
				break;
			}
		}

		const int finalizeResult = sqlite3_finalize(statement);
		if ((stepResult != SQLITE_DONE) && (stepResult != SQLITE_ROW))
		{
			errorMessage = L"Failed while reading SQLite table info: " +
			               ToWide(sqlite3_errmsg(m_database));
			return false;
		}

		if (finalizeResult != SQLITE_OK)
		{
			errorMessage = L"Failed to finalize SQLite table info statement: " +
			               ToWide(sqlite3_errmsg(m_database));
			return false;
		}

		if (columnExists)
		{
			return true;
		}

		const std::string alterSql =
		    "ALTER TABLE \"" + tableNameText + "\" ADD COLUMN " +
		    columnDefinitionText + ";";
		char* sqliteErrorMessage = nullptr;
		const int alterResult =
		    sqlite3_exec(m_database, alterSql.c_str(), nullptr, nullptr, &sqliteErrorMessage);
		if (alterResult != SQLITE_OK)
		{
			const std::string rawError = (sqliteErrorMessage != nullptr)
			                                 ? sqliteErrorMessage
			                                 : "unknown sqlite alter table error";
			errorMessage = L"Failed to migrate SQLite schema: " + ToWide(rawError);
			if (sqliteErrorMessage != nullptr)
			{
				sqlite3_free(sqliteErrorMessage);
			}
			return false;
		}

		return true;
#endif
	}

	std::string SessionLogRepository::ToUtf8(std::wstring_view value)
	{
		if (value.empty())
		{
			return std::string{};
		}

		const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (utf8Size <= 0)
		{
			return std::string{};
		}

		std::string utf8(utf8Size, '\0');
		const int convertedSize = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), utf8Size, nullptr, nullptr);
		if (convertedSize != utf8Size)
		{
			return std::string{};
		}

		return utf8;
	}

	std::wstring SessionLogRepository::ToWide(std::string_view value)
	{
		if (value.empty())
		{
			return std::wstring{};
		}

		const int wideSize = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
		if (wideSize <= 0)
		{
			return std::wstring{};
		}

		std::wstring wide(wideSize, L'\0');
		const int convertedSize = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), wideSize);
		if (convertedSize != wideSize)
		{
			return std::wstring{};
		}

		return wide;
	}
} // namespace BaseballSimulatorVR::Data

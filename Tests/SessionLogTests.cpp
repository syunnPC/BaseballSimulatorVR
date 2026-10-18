#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Data/SessionLogRepository.hpp"

#include <limits>

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
}

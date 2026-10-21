#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/AtomicFileWriter.hpp"
#include "../BaseballSimulatorVR/Common/JsonKeyValueParser.hpp"
#include "../BaseballSimulatorVR/Common/JsonWriter.hpp"
#include "../BaseballSimulatorVR/Data/PitchPresetJsonStorage.hpp"
#include "../BaseballSimulatorVR/Data/RuntimeSettingsJsonStorage.hpp"

#include <array>
#include <limits>
#include <locale>

namespace
{
	using namespace BaseballSimulatorVR::Common::JsonKeyValueParser;

	void CheckInvalidDocument(const std::string& text)
	{
		Tests::Check(
		    ReadIntResult(text, "n").status == KeyReadStatus::Invalid,
		    "Malformed JSON must invalidate existing keys");
		Tests::Check(
		    ReadIntResult(text, "missing").status == KeyReadStatus::Invalid,
		    "Malformed JSON must not look like a missing optional field");
	}

	void WriteFile(const std::filesystem::path& path, const std::string_view text)
	{
		std::ofstream output(path, std::ios::binary);
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
		output.close();
		Tests::Check(!output.fail(), "Test JSON file written");
	}

	class CommaDecimal final : public std::numpunct<char>
	{
		char do_decimal_point() const override
		{
			return ',';
		}
		char do_thousands_sep() const override
		{
			return '.';
		}
		std::string do_grouping() const override
		{
			return "\3";
		}
	};

	class LocaleGuard final
	{
	  public:
		LocaleGuard() : previous_(std::locale())
		{
			std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
		}
		~LocaleGuard()
		{
			std::locale::global(previous_);
		}

	  private:
		std::locale previous_;
	};

	void CheckDocumentModelAndDiagnostics()
	{
		using namespace BaseballSimulatorVR::Common::JsonKeyValueParser;
		const auto document = JsonDocument::Parse(
		    "{\n  \"a\": 1.5,\n  \"list\": [ { \"name\": \"x\", \"v\": 2 }, { \"name\": \"y\", \"v\": 3 } ],\n"
		    "  \"nested\": { \"flag\": true }\n}\n");
		Tests::Check(document.IsValid(), "A valid document parses once into a DOM");
		const auto root = document.Root();
		Tests::Check(root.ReadDouble("a").value == 1.5 && root.ReadBool("missing").status == KeyReadStatus::Missing,
		    "Several keys are queried from one parsed document");
		const auto list = root.ReadObjectArray("list");
		Tests::Check(list.value && list.value->size() == 2U && (*list.value)[1].ReadString("name").value == "y" &&
		    (*list.value)[1].Path() == "list[1]", "Array elements are object views with their member path");
		const auto nested = root.ReadObject("nested");
		Tests::Check(nested.value && nested.value->ReadBool("flag").value == true, "Nested objects can be queried");

		const auto typeError = (*list.value)[1].ReadBool("v");
		Tests::Check(typeError.status == KeyReadStatus::Invalid && typeError.diagnostic.line == 3U &&
		    typeError.diagnostic.path == "list[1].v" && !typeError.diagnostic.message.empty(),
		    "Type errors report the line and full key path of the offending member");
		Tests::Check(FormatDiagnostic(typeError.diagnostic).find("line 3, column") == 0U &&
		    FormatDiagnostic(typeError.diagnostic).find("key \"list[1].v\"") != std::string::npos,
		    "Diagnostics format as line, column and key");

		const auto broken = JsonDocument::Parse("{\n  \"a\": 1,\n  \"b\": [1, 2,]\n}");
		Tests::Check(!broken.IsValid() && broken.Error().line == 3U && broken.Error().column == 14U &&
		    broken.Error().path == "b", "Syntax errors report the exact line, column and member path");
		Tests::Check(ReadIntResult("{\n\"a\": 1,\n\"b\": tru }", "a").diagnostic.line == 3U,
		    "Legacy single-key reads surface the document syntax error position");
		const auto duplicate = JsonDocument::Parse("{\"k\": 1,\n \"k\": 2}");
		Tests::Check(!duplicate.IsValid() && duplicate.Error().line == 2U && duplicate.Error().path == "k" &&
		    duplicate.Error().message.find("duplicate") != std::string::npos, "Duplicate keys report their position");
		const auto japanese = JsonDocument::Parse("{\"名前\": \"値\", \"x\": nope}");
		Tests::Check(!japanese.IsValid() && japanese.Error().column == 18U,
		    "Columns count Unicode characters rather than UTF-8 bytes");

		const std::string bom = "\xEF\xBB\xBF{\"n\": 5}";
		Tests::Check(ReadInt(bom, "n") == 5 && JsonDocument::Parse(bom).IsValid(),
		    "A leading UTF-8 byte order mark (Notepad 'UTF-8 with BOM') is accepted");
		Tests::Check(!JsonDocument::Parse("{\"n\": 5}\xEF\xBB\xBF").IsValid() &&
		    !JsonDocument::Parse("\xEF\xBB\xBF\xEF\xBB\xBF{}").IsValid(),
		    "Only a single leading byte order mark is skipped");
		const auto bomError = JsonDocument::Parse("\xEF\xBB\xBF{\n\"n\": x}");
		Tests::Check(bomError.Error().line == 2U && bomError.Error().column == 6U,
		    "The byte order mark does not shift reported columns");
		const auto range = root.ReadInt("a");
		Tests::Check(range.status == KeyReadStatus::Invalid && range.diagnostic.path == "a",
		    "Integer conversion failures identify the key");
		JsonObjectView detached{};
		{
			const auto temporary = JsonDocument::Parse("{\"keep\": \"alive\"}");
			detached = temporary.Root();
		}
		Tests::Check(detached.ReadString("keep").value == "alive", "Views keep their document alive");
	}

	void CheckJsonWriter()
	{
		using namespace BaseballSimulatorVR::Common;
		Tests::Check(JsonWriter::FormatNumber(18.44) == "18.44" && JsonWriter::FormatNumber(0.1) == "0.1" &&
		    JsonWriter::FormatNumber(5.0) == "5.0" && JsonWriter::FormatNumber(-0.25) == "-0.25" &&
		    JsonWriter::FormatNumber(1e-7) == "1e-07" && JsonWriter::FormatNumber(30.555555555555554) ==
		        "30.555555555555554", "Numbers use the shortest round-trip representation");
		for (const double value : { 18.44, 0.1, 1.0 / 3.0, 6.02214076e23, -1e-300, 0.0064 })
		{
			const auto text = JsonWriter::FormatNumber(value);
			Tests::Check(text && JsonKeyValueParser::ReadDouble("{\"v\":" + *text + "}", "v") == value,
			    "Shortest formatting parses back to the identical double");
		}
		Tests::Check(!JsonWriter::FormatNumber(std::numeric_limits<double>::quiet_NaN()) &&
		    !JsonWriter::FormatNumber(std::numeric_limits<double>::infinity()), "Non-finite numbers are rejected");
		JsonWriter::ObjectBuilder builder;
		builder.AddComment("name", "Quote \" and backslash \\");
		builder.AddString("name", "\xE6\x97\xA5\n");
		std::vector<JsonWriter::ObjectBuilder> items(1U);
		items[0].AddInteger("i", -3);
		builder.AddObjectArray("items", items);
		const auto document = JsonKeyValueParser::JsonDocument::Parse(builder.BuildDocument());
		Tests::Check(document.IsValid() && document.Root().ReadString("name").value == "\xE6\x97\xA5\n" &&
		    document.Root().ReadString("_comment_name").value == "Quote \" and backslash \\" &&
		    document.Root().ReadObjectArray("items").value->front().ReadInt("i").value == -3,
		    "The object builder emits valid, escaped, nested JSON");
		Tests::Check(!builder.AddNumber("nan", std::numeric_limits<double>::quiet_NaN()) && builder.HasFailed(),
		    "Builders remember unrepresentable values");
	}

	void CheckAtomicSettingsWrites(const std::filesystem::path& directory)
	{
		using namespace BaseballSimulatorVR;
		const auto path = directory / L"設定.json";
		Tests::Check(Common::WriteFileAtomically(path, "{\"v\": 1}\n").succeeded, "Atomic write creates the file");
		Common::AtomicWriteOptions options;
		options.keepBackup = true;
		options.isBackupContentValid = [](const std::string_view text)
		{ return Common::JsonKeyValueParser::JsonDocument::Parse(text).IsValid(); };
		Tests::Check(Common::WriteFileAtomically(path, "{\"v\": 2}\n", options).succeeded, "Second write succeeds");
		Tests::Check(Tests::ReadFile(path) == "{\"v\": 2}\n" &&
		    Tests::ReadFile(Common::BuildBackupFilePath(path)) == "{\"v\": 1}\n",
		    "The previous good file is kept as a single .bak");
		WriteFile(path, "{\"v\": ");
		Tests::Check(Common::WriteFileAtomically(path, "{\"v\": 3}\n", options).succeeded &&
		    Tests::ReadFile(Common::BuildBackupFilePath(path)) == "{\"v\": 1}\n",
		    "A corrupted file never replaces the last good backup");
		Tests::Check(!Common::WriteFileAtomically(directory / L"missing" / L"x.json", "{}").succeeded,
		    "Writing into a missing directory fails without side effects");
		{
			std::ifstream locked(path, std::ios::binary);
			Tests::Check(locked.is_open(), "Hold the settings file open");
			Tests::Check(!Common::WriteFileAtomically(path, "{\"v\": 4}\n").succeeded &&
			    Tests::ReadFile(path) == "{\"v\": 3}\n", "A failed replacement leaves the previous content intact");
		}
		for (const auto& entry : std::filesystem::directory_iterator(directory))
			Tests::Check(entry.path().extension() != L".tmp", "Temporary files are removed after success and failure");
	}
} // namespace

void RunJsonParserTests()
{
	using namespace BaseballSimulatorVR;
	using namespace BaseballSimulatorVR::Common::JsonKeyValueParser;
	using namespace BaseballSimulatorVR::Data;

	const std::string scoped = R"({"description":"n","nested":{"n":99},"array":[{"n":98}],"n":7})";
	Tests::Check(ReadInt(scoped, "n") == 7, "Only direct members match a settings key");
	Tests::Check(
	    ReadIntResult(R"({"description":"n","other":4})", "n").status == KeyReadStatus::Missing,
	    "A matching string value is not a property");
	Tests::Check(
	    ReadIntResult(R"({"nested":{"n":1}})", "n").status == KeyReadStatus::Missing,
	    "A nested property cannot supply a missing root setting");
	Tests::Check(ReadInt(R"({"\u006e":8})", "n") == 8, "Escaped member names are decoded");
	Tests::Check(ReadInt(R"({"a\"b":8})", "a\"b") == 8, "Keys may contain escaped quotes");
	Tests::Check(
	    ReadDouble(R"({"n":-1.25e+2})", "n") == -125.0, "JSON decimal and exponent parsed");
	Tests::Check(
	    ReadInt(R"({"n":-2147483648})", "n") == (std::numeric_limits<int>::min)(),
	    "Signed lower bound");
	Tests::Check(
	    ReadInt(R"({"n":2147483647})", "n") == (std::numeric_limits<int>::max)(),
	    "Signed upper bound");
	Tests::Check(
	    ReadUnsignedInt(R"({"n":4294967295})", "n") == (std::numeric_limits<std::uint32_t>::max)(),
	    "Unsigned upper bound");
	Tests::Check(!ReadInt(R"({"n":2147483648})", "n"), "Signed overflow rejected");
	Tests::Check(!ReadUnsignedInt(R"({"n":4294967296})", "n"), "Unsigned overflow rejected");
	Tests::Check(!ReadUnsignedInt(R"({"n":-0})", "n"), "Unsigned signs rejected");
	Tests::Check(!ReadInt(R"({"n":1.0})", "n"), "Integer settings reject decimal tokens");
	Tests::Check(!ReadInt(R"({"n":1e0})", "n"), "Integer settings reject exponent tokens");
	Tests::Check(!ReadDouble(R"({"n":1e9999})", "n"), "Floating overflow rejected");
	Tests::Check(!ReadDouble(R"({"n":1e-9999})", "n"), "Floating underflow rejected");
	Tests::Check(
	    !ReadDouble(R"({"n":"2.5"})", "n"), "Quoted numbers cannot supply numeric settings");
	Tests::Check(
	    !ReadBool(R"({"n":"true"})", "n"), "Quoted booleans cannot supply boolean settings");
	Tests::Check(!ReadBool(R"({"n":1})", "n"), "Numbers cannot supply boolean settings");
	Tests::Check(ReadBool(R"({"n":false})", "n") == false, "False is a present value");
	Tests::Check(
	    ReadStringResult(R"({"n":true})", "n").status == KeyReadStatus::Invalid,
	    "String reads enforce type");
	Tests::Check(
	    ReadOptionalIntResult(R"({"n":"null"})", "n").status == KeyReadStatus::Invalid,
	    "Quoted null rejected");
	const auto nullValue = ReadOptionalIntResult(R"({"n":null})", "n");
	Tests::Check(
	    nullValue.status == KeyReadStatus::Valid && nullValue.value.has_value() &&
	        !nullValue.value->has_value(),
	    "Present null keeps the outer result engaged");
	std::optional<int> optionalValue = 3;
	Tests::Check(
	    TryReadOptionalInt(R"({"n":null})", "n", optionalValue) && !optionalValue,
	    "Null clears optional output");
	double unchanged = 42.0;
	Tests::Check(
	    !TryReadDouble(R"({"n":"bad"})", "n", unchanged) && unchanged == 42.0,
	    "Failed read preserves output");

	const auto unicode = ReadStringResult(R"({"n":"\u65e5\u672c\ud83d\ude80"})", "n");
	Tests::Check(
	    unicode.value == std::string("\xE6\x97\xA5\xE6\x9C\xAC\xF0\x9F\x9A\x80"),
	    "Unicode escapes decode to UTF-8");
	const auto escapes = ReadStringResult(R"({"n":"\"\\\/\b\f\n\r\t\u0000"})", "n");
	Tests::Check(
	    escapes.value == std::string("\"\\/\b\f\n\r\t\0", 9),
	    "All JSON escapes including embedded NUL preserved");
	const std::array malformed{ "",
		                        " ",
		                        "[]",
		                        "null",
		                        R"("n":1)",
		                        R"({"n":1)",
		                        R"({"n" 1})",
		                        R"({"n":})",
		                        R"({"n":1,})",
		                        R"({"n":1 "a":2})",
		                        R"({"n":1} trailing)",
		                        R"({"n":1}{})",
		                        R"({"n":01})",
		                        R"({"n":-01})",
		                        R"({"n":+1})",
		                        R"({"n":.5})",
		                        R"({"n":1.})",
		                        R"({"n":1e})",
		                        R"({"n":1e+})",
		                        R"({"n":0x10})",
		                        R"({"n":NaN})",
		                        R"({"n":Infinity})",
		                        R"({"n":truefalse})",
		                        R"({"n":1,"a":[1,]})",
		                        R"({"n":1,"a":[1 2]})",
		                        R"({"n":1,"a":{]})",
		                        R"({"n":1,"n":2})",
		                        R"({"n":1,"\u006e":2})",
		                        R"({"n":1,"a":{"x":1,"x":2}})",
		                        R"({"n":1,"a":"\q"})",
		                        R"({"n":1,"a":"\uD800"})",
		                        R"({"n":1,"a":"\uDC00"})",
		                        R"({"n":1,"a":"\uD800\u0041"})",
		                        R"({"n":1,"a":"\u123"})" };
	for (const char* text : malformed)
		CheckInvalidDocument(text);
	CheckInvalidDocument("{\"n\":1,\"a\":\"raw\nnewline\"}");
	CheckInvalidDocument("{\"n\":1,\"a\":\"" + std::string("\xC0\x80") + "\"}");
	CheckInvalidDocument("{\"n\":1,\"a\":\"" + std::string("\xED\xA0\x80") + "\"}");
	CheckInvalidDocument("{\"n\":1,\"a\":\"" + std::string("\xF4\x90\x80\x80") + "\"}");
	CheckInvalidDocument(std::string("{\"n\":1}") + '\0');
	CheckInvalidDocument(
	    "{\"n\":1,\"a\":" + std::string(150, '[') + "0" + std::string(150, ']') + "}");

	const std::string arrayText =
	    R"({"profiles":[{"name":"a } ] \\\"","extra":[1,{"n":2}]},{"name":"b"}]})";
	const auto array = ReadObjectArrayResult(arrayText, "profiles");
	Tests::Check(
	    array.status == KeyReadStatus::Valid && array.value->size() == 2U,
	    "Object arrays respect quoted brackets and nesting");
	Tests::Check(
	    ReadStringResult(std::string((*array.value)[1]), "name").value == "b",
	    "Object spans refer to complete values");
	Tests::Check(
	    ReadObjectArrayResult(R"({"profiles":[]})", "profiles").value->empty(),
	    "Empty array accepted");
	Tests::Check(
	    ReadObjectArrayResult(R"({"profiles":[{},1]})", "profiles").status ==
	        KeyReadStatus::Invalid,
	    "Profile arrays require objects");
	Tests::Check(
	    ReadObjectArrayResult(R"({"profiles":{}})", "profiles").status == KeyReadStatus::Invalid,
	    "Profile array type enforced");

	Tests::TempDirectory directory;
	const auto presetPath = directory.path / "pitch.json";
	const auto environmentPath = directory.path / "environment.json";
	const auto playPath = directory.path / "play.json";
	PitchPresetJsonStorage presets;
	RuntimeSettingsJsonStorage runtime;
	std::wstring error;
	PitchRuntimeSettings pitch{};
	pitch.pitcherThrowsLeftHanded = true;
	pitch.pitchProfiles.push_back({ L"\u65E5\u672C \U0001F680 \"quoted\"", pitch.pitchParameters });
	pitch.activePitchProfileIndex = 0;
	{
		LocaleGuard locale;
		Tests::Check(
		    presets.SavePitchSettings(presetPath.wstring(), pitch, error),
		    "Save pitch settings with non-JSON global numeric locale");
		Tests::Check(
		    runtime.SaveEnvironment(environmentPath.wstring(), {}, error),
		    "Save environment with non-JSON numeric locale");
		Tests::Check(
		    runtime.SavePlay(playPath.wstring(), {}, error),
		    "Save play settings with non-JSON numeric locale");
		Tests::Check(
		    presets.LoadPitchSettings(presetPath.wstring(), error).has_value(),
		    "Pitch output remains valid JSON under comma locale");
		Tests::Check(
		    runtime.LoadEnvironment(environmentPath.wstring(), error).has_value(),
		    "Environment round trip under comma locale");
		Tests::Check(
		    runtime.LoadPlay(playPath.wstring(), error).has_value(),
		    "Play round trip with null optional zone under comma locale");
	}
	const auto loadedPitch = presets.LoadPitchSettings(presetPath.wstring(), error);
	for (const auto material : { Physics::BatMaterial::Wood, Physics::BatMaterial::Metal,
	    Physics::BatMaterial::Urethane })
	{
		PlayRuntimeSettings play{};
		play.batMaterial = material;
		play.strikeZoneBottomMeters = .324;
		play.strikeZoneHeightMeters = .318;
		play.strikeZoneWidthMeters = .46;
		play.strikeZoneConfigured = true;
		Tests::Check(runtime.SavePlay(playPath.wstring(), play, error), "Save each bat material");
		const auto restored = runtime.LoadPlay(playPath.wstring(), error);
		Tests::Check(restored && restored->batMaterial == material && restored->strikeZoneConfigured &&
		    restored->strikeZoneBottomMeters == .324 && restored->strikeZoneHeightMeters == .318 &&
		    restored->strikeZoneWidthMeters == .46,
		    "Bat materials and the player's independent strike-zone dimensions survive JSON round trip");
	}
	for (const auto pitchMode : {Common::PitchTrajectoryDisplayMode::None,
	    Common::PitchTrajectoryDisplayMode::Full, Common::PitchTrajectoryDisplayMode::ZoneOnly})
	{
		for (const auto zoneMode : {Common::StrikeZoneDisplayMode::None,
		    Common::StrikeZoneDisplayMode::Plane, Common::StrikeZoneDisplayMode::Box})
		{
			PlayRuntimeSettings play{};
			play.pitchTrajectoryDisplayMode = pitchMode;
			play.strikeZoneDisplayMode = zoneMode;
			play.battedTrajectoryVisible = false;
			Tests::Check(runtime.SavePlay(playPath.wstring(), play, error), "Save all overlay display modes");
			const auto restored = runtime.LoadPlay(playPath.wstring(), error);
			Tests::Check(restored && restored->pitchTrajectoryDisplayMode == pitchMode &&
			    restored->strikeZoneDisplayMode == zoneMode && !restored->battedTrajectoryVisible,
			    "Every pitch and zone display mode round trips independently of batted visibility");
		}
	}
	const auto validPlay = Tests::ReadFile(playPath);
	for (const auto key : {"pitchTrajectoryDisplayMode", "strikeZoneDisplayMode"})
	{
		for (const auto invalidValue : {"\"Unknown\"", "\"none\"", "true", "0", "null"})
		{
			auto invalid = validPlay;
			const auto begin = invalid.find("\"" + std::string(key) + "\":");
			invalid.replace(begin, invalid.find('\n', begin) - begin,
			    "\"" + std::string(key) + "\": " + invalidValue + ",");
			WriteFile(playPath, invalid);
			Tests::Check(!runtime.LoadPlay(playPath.wstring(), error) && !error.empty(),
			    "Unknown or incorrectly typed display mode is rejected");
		}
		auto missing = validPlay;
		const auto begin = missing.find("\"" + std::string(key) + "\":");
		missing.erase(begin, missing.find('\n', begin) - begin);
		WriteFile(playPath, missing);
		Tests::Check(!runtime.LoadPlay(playPath.wstring(), error), "Both display modes are required");
	}
	WriteFile(playPath, validPlay);
	for (const bool invalidPitchMode : {false, true})
	{
		PlayRuntimeSettings invalid{};
		if (invalidPitchMode) invalid.pitchTrajectoryDisplayMode = static_cast<Common::PitchTrajectoryDisplayMode>(99);
		else invalid.strikeZoneDisplayMode = static_cast<Common::StrikeZoneDisplayMode>(99);
		Tests::Check(!runtime.SavePlay(playPath.wstring(), invalid, error) && Tests::ReadFile(playPath) == validPlay,
		    "Invalid display modes preserve the existing settings file on save");
	}
	struct InvalidZoneField { std::string_view key, value; };
	for (const auto& field : {InvalidZoneField{"strikeZoneBottomMeters", "0.1"},
	    InvalidZoneField{"strikeZoneBottomMeters", "2.4"}, InvalidZoneField{"strikeZoneHeightMeters", "0.19"},
	    InvalidZoneField{"strikeZoneHeightMeters", "1.61"}, InvalidZoneField{"strikeZoneWidthMeters", "0.19"},
	    InvalidZoneField{"strikeZoneWidthMeters", "0.81"}, InvalidZoneField{"strikeZoneConfigured", "0"},
	    InvalidZoneField{"strikeZoneBottomMeters", "null"}, InvalidZoneField{"strikeZoneHeightMeters", "\"0.5\""}})
	{
		auto invalid = validPlay;
		const auto begin = invalid.find("\"" + std::string(field.key) + "\":");
		invalid.replace(begin, invalid.find('\n', begin) - begin,
		    "\"" + std::string(field.key) + "\": " + std::string(field.value) + ",");
		WriteFile(playPath, invalid);
		Tests::Check(!runtime.LoadPlay(playPath.wstring(), error) && !error.empty(),
		    "Player strike-zone dimensions and configured metadata reject incorrect types and out-of-range geometry");
	}
	for (const auto key : {"strikeZoneBottomMeters", "strikeZoneHeightMeters", "strikeZoneWidthMeters", "strikeZoneConfigured"})
	{
		auto missing = validPlay;
		const auto begin = missing.find("\"" + std::string(key) + "\":");
		missing.erase(begin, missing.find('\n', begin) - begin);
		WriteFile(playPath, missing);
		Tests::Check(!runtime.LoadPlay(playPath.wstring(), error), "Every player strike-zone field is required");
	}
	WriteFile(playPath, validPlay);
	for (const double invalidHeight : {-.1, .19, 1.61, std::numeric_limits<double>::quiet_NaN()})
	{
		PlayRuntimeSettings invalid{};
		invalid.strikeZoneHeightMeters = invalidHeight;
		Tests::Check(!runtime.SavePlay(playPath.wstring(), invalid, error) && Tests::ReadFile(playPath) == validPlay,
		    "Invalid player-zone saves preserve the previous play settings file");
	}
	for (const auto invalidValue : { "\"wood\"", "\"Carbon\"", "0", "null", "true" })
	{
		auto invalid = validPlay;
		const auto begin = invalid.find("\"batMaterial\":");
		invalid.replace(begin, invalid.find('\n', begin) - begin,
		    std::string("\"batMaterial\": ") + invalidValue + ",");
		WriteFile(playPath, invalid);
		Tests::Check(!runtime.LoadPlay(playPath.wstring(), error) && !error.empty(),
		    "Unknown or incorrectly typed bat material is rejected");
	}
	{
		auto missing = validPlay;
		const auto begin = missing.find("\"batMaterial\":");
		missing.erase(begin, missing.find('\n', begin) - begin);
		WriteFile(playPath, missing);
		Tests::Check(!runtime.LoadPlay(playPath.wstring(), error), "Missing bat material is rejected");
	}
	WriteFile(playPath, validPlay);
	PlayRuntimeSettings invalidPlay{};
	invalidPlay.batMaterial = static_cast<Physics::BatMaterial>(99);
	Tests::Check(!runtime.SavePlay(playPath.wstring(), invalidPlay, error) &&
	    Tests::ReadFile(playPath) == validPlay,
	    "Saving an invalid bat material preserves the previous settings file");

	EnvironmentRuntimeSettings environment{};
	environment.ballProperties.restitutionAtReferenceSpeed = .62;
	Tests::Check(runtime.SaveEnvironment(environmentPath.wstring(), environment, error),
	    "Save ball restitution as a ball property");
	const auto restoredEnvironment = runtime.LoadEnvironment(environmentPath.wstring(), error);
	Tests::Check(restoredEnvironment && restoredEnvironment->ballProperties.restitutionAtReferenceSpeed == .62,
	    "Ball restitution survives an environment JSON round trip");
	const auto validEnvironment = Tests::ReadFile(environmentPath);
	Tests::Check(validEnvironment.find("\"batBallRestitution\"") == std::string::npos,
	    "Environment output contains no obsolete bat restitution setting");
	Tests::Check(validEnvironment.find("\"strikeZoneBottomMeters\"") == std::string::npos &&
	    validEnvironment.find("\"strikeZoneHeightMeters\"") == std::string::npos &&
	    validEnvironment.find("\"strikeZoneWidthMeters\"") == std::string::npos,
	    "Environment files contain no player strike-zone settings");
	for (const auto invalidValue : { "-0.01", "1.01", "\"0.45\"", "null", "true" })
	{
		auto invalid = validEnvironment;
		const auto begin = invalid.find("\"ballRestitutionAtReferenceSpeed\":");
		invalid.replace(begin, invalid.find('\n', begin) - begin,
		    std::string("\"ballRestitutionAtReferenceSpeed\": ") + invalidValue + ",");
		WriteFile(environmentPath, invalid);
		Tests::Check(!runtime.LoadEnvironment(environmentPath.wstring(), error) && !error.empty(),
		    "Environment rejects invalid ball restitution instead of substituting a default");
	}
	{
		auto missing = validEnvironment;
		const auto begin = missing.find("\"ballRestitutionAtReferenceSpeed\":");
		missing.erase(begin, missing.find('\n', begin) - begin);
		WriteFile(environmentPath, missing);
		Tests::Check(!runtime.LoadEnvironment(environmentPath.wstring(), error),
		    "Environment requires the ball restitution setting");
	}
	WriteFile(environmentPath, validEnvironment);
	for (const auto value : { -.01, 1.01, std::numeric_limits<double>::infinity(),
	    std::numeric_limits<double>::quiet_NaN() })
	{
		environment.ballProperties.restitutionAtReferenceSpeed = value;
		Tests::Check(!runtime.SaveEnvironment(environmentPath.wstring(), environment, error) &&
		    Tests::ReadFile(environmentPath) == validEnvironment,
		    "Saving invalid ball restitution leaves the previous environment file intact");
	}
	Tests::Check(loadedPitch && loadedPitch->pitcherThrowsLeftHanded,
	    "Left-handed pitcher setting survives a JSON save/load round trip");
	Tests::Check(
	    loadedPitch && loadedPitch->pitchProfiles[0].name == pitch.pitchProfiles[0].name,
	    "Unicode and escaped profile name round trip");
	const auto preset = Tests::ReadFile(presetPath);
	{
		auto omittedHand = preset;
		const auto handBegin = omittedHand.find("\"pitcherThrowsLeftHanded\":");
		omittedHand.erase(handBegin, omittedHand.find('\n', handBegin) - handBegin);
		WriteFile(presetPath, omittedHand);
		const auto defaultHand = presets.LoadPitchSettings(presetPath.wstring(), error);
		Tests::Check(defaultHand && !defaultHand->pitcherThrowsLeftHanded,
		    "Omitting the root throwing-hand field selects the right-handed default");
	}
	for (const auto invalidField :
	     { "\"activePitchProfileIndex\": \"0\",", "\"randomPitchProfileEnabled\": 1,",
	       "\"environmentSettingsFile\": false,", "\"pitcherThrowsLeftHanded\": \"true\",",
	       "\"pitcherThrowsLeftHanded\": 1,", "\"pitcherThrowsLeftHanded\": null," })
	{
		// Remove the existing property to avoid testing duplicate-key rejection instead of its
		// type.
		std::string modified = preset;
		const std::string field(invalidField);
		const auto keyEnd = field.find(':');
		const auto begin = modified.find(field.substr(0U, keyEnd));
		const auto end = modified.find('\n', begin);
		modified.replace(begin, end - begin, field);
		WriteFile(presetPath, modified);
		Tests::Check(
		    !presets.LoadPitchSettings(presetPath.wstring(), error) && !error.empty(),
		    "Invalid optional pitch field is reported");
	}
	WriteFile(
	    environmentPath,
	    R"({"pitchDistanceMeters":18.44,"enableMoundHeightOffset":false,"moundHeightOffsetMeters":0.18,"ballRestitutionAtReferenceSpeed":0.45,"integratorType":"Rk4"} garbage)");
	Tests::Check(
	    !runtime.LoadEnvironment(environmentPath.wstring(), error),
	    "Environment rejects trailing document garbage");
	WriteFile(
	    playPath,
	    R"({"duelPitchIntervalSeconds":5,"playbackSpeedPercent":100,"batMaterial":"Wood","strikeZoneBottomMeters":0.55,"strikeZoneHeightMeters":1.0,"strikeZoneWidthMeters":0.44,"strikeZoneConfigured":false,"pitchTrajectoryDisplayMode":"Full","strikeZoneDisplayMode":"Box","duelTargetZoneIndex":"null"})");
	Tests::Check(!runtime.LoadPlay(playPath.wstring(), error), "Play settings reject quoted null");
	WriteFile(
	    playPath,
	    R"({"duelPitchIntervalSeconds":5,"playbackSpeedPercent":100,"batMaterial":"Wood","strikeZoneBottomMeters":0.55,"strikeZoneHeightMeters":1.0,"strikeZoneWidthMeters":0.44,"strikeZoneConfigured":false,"pitchTrajectoryDisplayMode":"Full","strikeZoneDisplayMode":"Box","duelTargetZoneIndex":null})");
	Tests::Check(
	    runtime.LoadPlay(playPath.wstring(), error).has_value(),
	    "Play settings accept explicit null");

	CheckDocumentModelAndDiagnostics();
	CheckJsonWriter();
	{
		Tests::TempDirectory atomicDirectory;
		CheckAtomicSettingsWrites(atomicDirectory.path);
	}

	// Saved files use shortest round-trip numbers and keep a backup of the previous version.
	EnvironmentRuntimeSettings shortest{};
	shortest.pitchDistanceMeters = 18.44;
	shortest.windShearFactor = 0.1;
	Tests::Check(runtime.SaveEnvironment(environmentPath.wstring(), shortest, error), "Save environment for formatting");
	const auto shortestText = Tests::ReadFile(environmentPath);
	Tests::Check(shortestText.find("\"pitchDistanceMeters\": 18.44,") != std::string::npos &&
	    shortestText.find("\"windShearFactor\": 0.1,") != std::string::npos &&
	    shortestText.find("0000000") == std::string::npos,
	    "Environment numbers are written without fixed max_digits10 noise");
	Tests::Check(std::filesystem::exists(Common::BuildBackupFilePath(environmentPath)),
	    "Replacing a valid environment file keeps a .bak copy");

	// New optional environment fields: defaults when absent, persisted, validated.
	WriteFile(environmentPath,
	    R"({"pitchDistanceMeters":18.44,"enableMoundHeightOffset":false,"moundHeightOffsetMeters":0.18,"ballRestitutionAtReferenceSpeed":0.45})");
	const auto legacyEnvironment = runtime.LoadEnvironment(environmentPath.wstring(), error);
	Tests::Check(legacyEnvironment && legacyEnvironment->altitudeMeters == 0.0 &&
	    legacyEnvironment->temperatureCelsius == 15.0 && legacyEnvironment->relativeHumidityPercent == 0.0 &&
	    legacyEnvironment->batMassKilograms == 0.885, "Old environment files load with the new field defaults");
	EnvironmentRuntimeSettings atmosphere{};
	atmosphere.altitudeMeters = 1609.3;
	atmosphere.temperatureCelsius = 31.5;
	atmosphere.relativeHumidityPercent = 62.0;
	atmosphere.batMassKilograms = 0.92;
	Tests::Check(runtime.SaveEnvironment(environmentPath.wstring(), atmosphere, error), "Save atmosphere fields");
	const auto restoredAtmosphere = runtime.LoadEnvironment(environmentPath.wstring(), error);
	Tests::Check(restoredAtmosphere && restoredAtmosphere->altitudeMeters == 1609.3 &&
	    restoredAtmosphere->temperatureCelsius == 31.5 && restoredAtmosphere->relativeHumidityPercent == 62.0 &&
	    restoredAtmosphere->batMassKilograms == 0.92, "Altitude, temperature, humidity and bat mass round trip");
	const auto atmosphereText = Tests::ReadFile(environmentPath);
	struct InvalidEnvironmentField { std::string_view key, value; };
	for (const auto& field : {InvalidEnvironmentField{"altitudeMeters", "-600"},
	    InvalidEnvironmentField{"altitudeMeters", "6000"}, InvalidEnvironmentField{"temperatureCelsius", "-41"},
	    InvalidEnvironmentField{"temperatureCelsius", "61"}, InvalidEnvironmentField{"relativeHumidityPercent", "101"},
	    InvalidEnvironmentField{"relativeHumidityPercent", "-1"}, InvalidEnvironmentField{"batMassKilograms", "0.1"},
	    InvalidEnvironmentField{"batMassKilograms", "2"}, InvalidEnvironmentField{"batMassKilograms", "\"0.9\""}})
	{
		auto invalid = atmosphereText;
		const auto begin = invalid.find("\"" + std::string(field.key) + "\":");
		invalid.replace(begin, invalid.find('\n', begin) - begin,
		    "\"" + std::string(field.key) + "\": " + std::string(field.value) + ",");
		WriteFile(environmentPath, invalid);
		Tests::Check(!runtime.LoadEnvironment(environmentPath.wstring(), error) &&
		    error.find(std::wstring(field.key.begin(), field.key.end())) != std::wstring::npos &&
		    error.find(L"line ") != std::wstring::npos,
		    "Out-of-range atmosphere and bat-mass values are rejected with key and line");
	}
	for (const double invalidAltitude : {-501.0, 5001.0, std::numeric_limits<double>::quiet_NaN()})
	{
		EnvironmentRuntimeSettings invalid{};
		invalid.altitudeMeters = invalidAltitude;
		Tests::Check(!runtime.SaveEnvironment(environmentPath.wstring(), invalid, error) && !error.empty(),
		    "Invalid atmosphere values are not saved");
	}

	// Error messages name the line and key; a Notepad BOM is accepted by the storage layer.
	WriteFile(environmentPath,
	    "\xEF\xBB\xBF{\n  \"pitchDistanceMeters\": 18.44,\n  \"enableMoundHeightOffset\": false,\n"
	    "  \"moundHeightOffsetMeters\": 0.18,\n  \"ballRestitutionAtReferenceSpeed\": 0.45\n}\n");
	Tests::Check(runtime.LoadEnvironment(environmentPath.wstring(), error).has_value(),
	    "Settings saved as UTF-8 with BOM load");
	WriteFile(environmentPath,
	    "{\n  \"pitchDistanceMeters\": 18.44,\n  \"enableMoundHeightOffset\": \"no\",\n"
	    "  \"moundHeightOffsetMeters\": 0.18,\n  \"ballRestitutionAtReferenceSpeed\": 0.45\n}\n");
	Tests::Check(!runtime.LoadEnvironment(environmentPath.wstring(), error) &&
	    error.find(L"line 3") != std::wstring::npos && error.find(L"enableMoundHeightOffset") != std::wstring::npos,
	    "A wrongly typed setting is reported with its line and key");
	WriteFile(environmentPath, "{\n  \"pitchDistanceMeters\": 18.44,\n  \"enableMoundHeightOffset\": false\n  \"x\": 1\n}");
	Tests::Check(!runtime.LoadEnvironment(environmentPath.wstring(), error) && error.find(L"line 4") != std::wstring::npos,
	    "A syntax error is reported with its line");

	// Pitch selection state lives in PlaySettings.
	PlayRuntimeSettings selection{};
	selection.selectedPitchSettingsFile = L"PitchSettings.Difficulty.Easy.LHP.json";
	selection.activePitchProfileIndex = 2;
	selection.randomPitchProfileEnabled = false;
	selection.settingsLayoutVersion = CurrentSettingsLayoutVersion;
	Tests::Check(runtime.SavePlay(playPath.wstring(), selection, error), "Save play selection state");
	const auto restoredSelection = runtime.LoadPlay(playPath.wstring(), error);
	Tests::Check(restoredSelection && restoredSelection->selectedPitchSettingsFile == selection.selectedPitchSettingsFile &&
	    restoredSelection->activePitchProfileIndex == 2 && restoredSelection->randomPitchProfileEnabled == false &&
	    restoredSelection->settingsLayoutVersion == CurrentSettingsLayoutVersion,
	    "Selected pitch file, profile index, random flag and layout version round trip");
	Tests::Check(runtime.SavePlay(playPath.wstring(), PlayRuntimeSettings{}, error), "Save default selection state");
	const auto defaultSelection = runtime.LoadPlay(playPath.wstring(), error);
	Tests::Check(defaultSelection && defaultSelection->selectedPitchSettingsFile.empty() &&
	    !defaultSelection->activePitchProfileIndex && !defaultSelection->randomPitchProfileEnabled &&
	    defaultSelection->settingsLayoutVersion == 0, "Unset selection fields stay unset");

	// Display name, mirror flag and the generated opposite-hand variant.
	PitchRuntimeSettings mirrorSource{};
	mirrorSource.displayName = L"難易度 標準";
	mirrorSource.mirrorForLeftHandedPitcher = true;
	mirrorSource.pitchParameters.spinAxis = { 0.6, 0.8, 0.0 };
	mirrorSource.pitchProfiles = { { L"スライダー", mirrorSource.pitchParameters } };
	mirrorSource.pitchProfiles[0].pitchParameters.spinAxis = { 0.2, -0.4, 0.3 };
	mirrorSource.activePitchProfileIndex = 0;
	Tests::Check(presets.SavePitchSettings(presetPath.wstring(), mirrorSource, error), "Save mirror source");
	const auto mirrorLoaded = presets.LoadPitchSettings(presetPath.wstring(), error);
	Tests::Check(mirrorLoaded && mirrorLoaded->displayName == L"難易度 標準" && mirrorLoaded->mirrorForLeftHandedPitcher,
	    "displayName and mirrorForLeftHandedPitcher round trip");
	const auto mirrored = MirrorPitchSettingsForOppositeHand(*mirrorLoaded);
	const auto& mirroredAxis = mirrored.pitchProfiles[0].pitchParameters.spinAxis;
	Tests::Check(mirrored.pitcherThrowsLeftHanded && !mirrored.mirrorForLeftHandedPitcher &&
	    mirroredAxis.x == 0.2 && mirroredAxis.y == 0.4 && mirroredAxis.z == -0.3 &&
	    mirrored.pitchParameters.spinAxis.y == -0.8 && mirrored.displayName == mirrorLoaded->displayName &&
	    mirrored.pitchProfiles[0].name == L"スライダー",
	    "The generated left-handed variant mirrors the spin axis and flips the throwing hand");
	Tests::Check(Tests::ReadFile(presetPath).find("0000000000") == std::string::npos,
	    "Pitch settings numbers use shortest round-trip formatting");
	WriteFile(presetPath, "\xEF\xBB\xBF" + Tests::ReadFile(presetPath));
	Tests::Check(presets.LoadPitchSettings(presetPath.wstring(), error).has_value(), "Pitch settings with BOM load");
	{
		auto invalid = Tests::ReadFile(presetPath);
		const auto begin = invalid.rfind("\"spinRpm\":");
		invalid.replace(begin, invalid.find('\n', begin) - begin, "\"spinRpm\": \"fast\",");
		WriteFile(presetPath, invalid);
		Tests::Check(!presets.LoadPitchSettings(presetPath.wstring(), error) &&
		    error.find(L"pitchProfiles[0].spinRpm") != std::wstring::npos && error.find(L"line ") != std::wstring::npos,
		    "Profile errors identify the profile, key and line");
	}
}

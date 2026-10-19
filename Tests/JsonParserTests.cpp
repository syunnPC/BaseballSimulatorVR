#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/JsonKeyValueParser.hpp"
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
	const std::wstring invalidUnicode(1, static_cast<wchar_t>(0xD800));
	for (const bool invalidEnvironmentPath : { false, true })
	{
		auto invalid = pitch;
		if (invalidEnvironmentPath) invalid.environmentSettingsFile = invalidUnicode;
		else invalid.pitchProfiles.push_back({ invalidUnicode, pitch.pitchParameters });
		Tests::Check(!presets.SavePitchSettings(presetPath.wstring(), invalid, error) && !error.empty(),
		    "Invalid UTF-16 in environment paths and pitch profiles is rejected on save");
		Tests::Check(Tests::ReadFile(presetPath) == preset,
		    "String conversion failures preserve the complete existing pitch settings file");
	}
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
}

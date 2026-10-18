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
	    R"({"pitchDistanceMeters":18.44,"enableMoundHeightOffset":false,"moundHeightOffsetMeters":0.18,"integratorType":"Rk4"} garbage)");
	Tests::Check(
	    !runtime.LoadEnvironment(environmentPath.wstring(), error),
	    "Environment rejects trailing document garbage");
	WriteFile(
	    playPath,
	    R"({"duelPitchIntervalSeconds":5,"playbackSpeedPercent":100,"duelTargetZoneIndex":"null"})");
	Tests::Check(!runtime.LoadPlay(playPath.wstring(), error), "Play settings reject quoted null");
	WriteFile(
	    playPath,
	    R"({"duelPitchIntervalSeconds":5,"playbackSpeedPercent":100,"duelTargetZoneIndex":null})");
	Tests::Check(
	    runtime.LoadPlay(playPath.wstring(), error).has_value(),
	    "Play settings accept explicit null");
}

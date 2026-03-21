#include "PitchPresetJsonStorage.hpp"
#include "../Common/JsonKeyValueParser.hpp"
#include "../Framework.hpp"

#include <filesystem>
#include <fstream>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		constexpr std::string_view DefaultEnvironmentSettingsFileName =
		    "EnvironmentSettings.user.json";

		enum class JsonArrayLookupStatus : std::uint8_t
		{
			Missing,
			Found,
			Invalid
		};

		struct JsonArrayLookupResult final
		{
			JsonArrayLookupStatus status{ JsonArrayLookupStatus::Missing };
			std::string_view value{};
		};

		enum class JsonStringLookupStatus : std::uint8_t
		{
			Missing,
			Found,
			Invalid
		};

		struct JsonStringLookupResult final
		{
			JsonStringLookupStatus status{ JsonStringLookupStatus::Missing };
			std::string value{};
		};

		[[nodiscard]] std::size_t SkipJsonWhitespace(
		    const std::string_view jsonText,
		    std::size_t cursor)
		{
			while ((cursor < jsonText.size()) &&
			       std::isspace(static_cast<unsigned char>(jsonText[cursor])))
			{
				++cursor;
			}
			return cursor;
		}

		[[nodiscard]] std::optional<std::size_t> FindMatchingJsonToken(
		    const std::string_view jsonText,
		    const std::size_t openingIndex,
		    const char openingToken,
		    const char closingToken)
		{
			if ((openingIndex >= jsonText.size()) ||
			    (jsonText[openingIndex] != openingToken))
			{
				return std::nullopt;
			}

			std::size_t nestedDepth = 0;
			bool isInsideString = false;
			bool isEscaped = false;
			for (std::size_t index = openingIndex + 1;
			     index < jsonText.size();
			     ++index)
			{
				const char currentCharacter = jsonText[index];
				if (isInsideString)
				{
					if (isEscaped)
					{
						isEscaped = false;
						continue;
					}
					if (currentCharacter == '\\')
					{
						isEscaped = true;
						continue;
					}
					if (currentCharacter == '"')
					{
						isInsideString = false;
					}
					continue;
				}

				if (currentCharacter == '"')
				{
					isInsideString = true;
					continue;
				}
				if (currentCharacter == openingToken)
				{
					++nestedDepth;
					continue;
				}
				if (currentCharacter == closingToken)
				{
					if (nestedDepth == 0)
					{
						return index;
					}
					--nestedDepth;
				}
			}

			return std::nullopt;
		}

		[[nodiscard]] bool TryParseFourHexDigits(
		    const std::string_view jsonText,
		    const std::size_t startIndex,
		    std::uint32_t& codePoint) noexcept
		{
			if ((startIndex + 4U) > jsonText.size())
			{
				return false;
			}

			codePoint = 0;
			for (std::size_t digitIndex = 0; digitIndex < 4U; ++digitIndex)
			{
				const char currentCharacter = jsonText[startIndex + digitIndex];
				std::uint32_t digitValue = 0;
				if ((currentCharacter >= '0') && (currentCharacter <= '9'))
				{
					digitValue = static_cast<std::uint32_t>(currentCharacter - '0');
				}
				else if ((currentCharacter >= 'a') && (currentCharacter <= 'f'))
				{
					digitValue =
					    10U + static_cast<std::uint32_t>(currentCharacter - 'a');
				}
				else if ((currentCharacter >= 'A') && (currentCharacter <= 'F'))
				{
					digitValue =
					    10U + static_cast<std::uint32_t>(currentCharacter - 'A');
				}
				else
				{
					return false;
				}

				codePoint = (codePoint << 4U) | digitValue;
			}

			return true;
		}

		[[nodiscard]] bool AppendUtf8CodePoint(
		    std::string& output,
		    const std::uint32_t codePoint)
		{
			if (codePoint <= 0x7FU)
			{
				output.push_back(static_cast<char>(codePoint));
				return true;
			}
			if (codePoint <= 0x7FFU)
			{
				output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
				output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
				return true;
			}
			if ((codePoint >= 0xD800U) && (codePoint <= 0xDFFFU))
			{
				return false;
			}
			if (codePoint <= 0xFFFFU)
			{
				output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
				output.push_back(
				    static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
				output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
				return true;
			}
			if (codePoint <= 0x10FFFFU)
			{
				output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
				output.push_back(
				    static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
				output.push_back(
				    static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
				output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
				return true;
			}

			return false;
		}

		[[nodiscard]] bool TryDecodeJsonStringValue(
		    const std::string_view jsonText,
		    const std::size_t openingQuoteIndex,
		    std::string& decodedValue)
		{
			if ((openingQuoteIndex >= jsonText.size()) ||
			    (jsonText[openingQuoteIndex] != '"'))
			{
				return false;
			}

			decodedValue.clear();
			for (std::size_t cursor = openingQuoteIndex + 1U;
			     cursor < jsonText.size();
			     ++cursor)
			{
				const char currentCharacter = jsonText[cursor];
				if (currentCharacter == '"')
				{
					return true;
				}
				if (currentCharacter != '\\')
				{
					decodedValue.push_back(currentCharacter);
					continue;
				}
				if ((cursor + 1U) >= jsonText.size())
				{
					return false;
				}

				const char escapedCharacter = jsonText[++cursor];
				switch (escapedCharacter)
				{
					case '"':
					case '\\':
					case '/':
						decodedValue.push_back(escapedCharacter);
						break;
					case 'b':
						decodedValue.push_back('\b');
						break;
					case 'f':
						decodedValue.push_back('\f');
						break;
					case 'n':
						decodedValue.push_back('\n');
						break;
					case 'r':
						decodedValue.push_back('\r');
						break;
					case 't':
						decodedValue.push_back('\t');
						break;
					case 'u':
					{
						std::uint32_t codePoint = 0;
						if (!TryParseFourHexDigits(
						        jsonText,
						        cursor + 1U,
						        codePoint))
						{
							return false;
						}
						cursor += 4U;
						if ((codePoint >= 0xD800U) && (codePoint <= 0xDBFFU))
						{
							if ((cursor + 6U) >= jsonText.size() ||
							    (jsonText[cursor + 1U] != '\\') ||
							    (jsonText[cursor + 2U] != 'u'))
							{
								return false;
							}

							std::uint32_t lowSurrogate = 0;
							if (!TryParseFourHexDigits(
							        jsonText,
							        cursor + 3U,
							        lowSurrogate) ||
							    (lowSurrogate < 0xDC00U) ||
							    (lowSurrogate > 0xDFFFU))
							{
								return false;
							}
							cursor += 6U;
							codePoint = 0x10000U +
							            (((codePoint - 0xD800U) << 10U) |
							             (lowSurrogate - 0xDC00U));
						}
						else if ((codePoint >= 0xDC00U) &&
						         (codePoint <= 0xDFFFU))
						{
							return false;
						}

						if (!AppendUtf8CodePoint(decodedValue, codePoint))
						{
							return false;
						}
						break;
					}
					default:
						return false;
				}
			}

			return false;
		}

		[[nodiscard]] JsonStringLookupResult FindJsonStringByKey(
		    const std::string& jsonText,
		    const std::string_view key)
		{
			const std::string keyToken = "\"" + std::string(key) + "\"";
			const std::size_t keyPosition = jsonText.find(keyToken);
			if (keyPosition == std::string::npos)
			{
				return JsonStringLookupResult{};
			}

			const std::size_t colonPosition =
			    jsonText.find(':', keyPosition + keyToken.size());
			if (colonPosition == std::string::npos)
			{
				return JsonStringLookupResult{
					JsonStringLookupStatus::Invalid,
					{}
				};
			}

			const std::string_view jsonView{ jsonText };
			const std::size_t valueBegin =
			    SkipJsonWhitespace(jsonView, colonPosition + 1U);
			if ((valueBegin >= jsonView.size()) || (jsonView[valueBegin] != '"'))
			{
				return JsonStringLookupResult{
					JsonStringLookupStatus::Invalid,
					{}
				};
			}

			JsonStringLookupResult result{
				JsonStringLookupStatus::Found,
				{}
			};
			if (!TryDecodeJsonStringValue(jsonView, valueBegin, result.value))
			{
				result.status = JsonStringLookupStatus::Invalid;
				result.value.clear();
			}
			return result;
		}

		[[nodiscard]] std::string EscapeJsonString(const std::string_view value)
		{
			constexpr char HexDigits[] = "0123456789ABCDEF";
			std::string escapedValue{};
			escapedValue.reserve(value.size() + 8U);
			for (const unsigned char currentCharacter : value)
			{
				switch (currentCharacter)
				{
					case '\"':
						escapedValue += "\\\"";
						break;
					case '\\':
						escapedValue += "\\\\";
						break;
					case '\b':
						escapedValue += "\\b";
						break;
					case '\f':
						escapedValue += "\\f";
						break;
					case '\n':
						escapedValue += "\\n";
						break;
					case '\r':
						escapedValue += "\\r";
						break;
					case '\t':
						escapedValue += "\\t";
						break;
					default:
						if (currentCharacter < 0x20U)
						{
							escapedValue += "\\u00";
							escapedValue.push_back(
							    HexDigits[(currentCharacter >> 4U) & 0x0FU]);
							escapedValue.push_back(
							    HexDigits[currentCharacter & 0x0FU]);
						}
						else
						{
							escapedValue.push_back(
							    static_cast<char>(currentCharacter));
						}
						break;
				}
			}

			return escapedValue;
		}

		[[nodiscard]] JsonArrayLookupResult FindJsonArrayByKey(
		    const std::string& jsonText,
		    const std::string_view key)
		{
			const std::string keyToken = "\"" + std::string(key) + "\"";
			const std::size_t keyPosition = jsonText.find(keyToken);
			if (keyPosition == std::string::npos)
			{
				return JsonArrayLookupResult{};
			}

			const std::size_t colonPosition =
			    jsonText.find(':', keyPosition + keyToken.size());
			if (colonPosition == std::string::npos)
			{
				return JsonArrayLookupResult{
					JsonArrayLookupStatus::Invalid,
					{}
				};
			}

			const std::string_view jsonView{ jsonText };
			const std::size_t valueBegin =
			    SkipJsonWhitespace(jsonView, colonPosition + 1U);
			if ((valueBegin >= jsonView.size()) || (jsonView[valueBegin] != '['))
			{
				return JsonArrayLookupResult{
					JsonArrayLookupStatus::Invalid,
					{}
				};
			}

			const std::optional<std::size_t> valueEnd =
			    FindMatchingJsonToken(jsonView, valueBegin, '[', ']');
			if (!valueEnd.has_value())
			{
				return JsonArrayLookupResult{
					JsonArrayLookupStatus::Invalid,
					{}
				};
			}

			return JsonArrayLookupResult{
				JsonArrayLookupStatus::Found,
				jsonView.substr(valueBegin, (*valueEnd - valueBegin) + 1U)
			};
		}

		[[nodiscard]] bool TryExtractJsonObjectsFromArray(
		    const std::string_view jsonArrayText,
		    std::vector<std::string_view>& objectTexts)
		{
			objectTexts.clear();

			std::size_t cursor = SkipJsonWhitespace(jsonArrayText, 0);
			if ((cursor >= jsonArrayText.size()) ||
			    (jsonArrayText[cursor] != '['))
			{
				return false;
			}

			cursor = SkipJsonWhitespace(jsonArrayText, cursor + 1U);
			if ((cursor < jsonArrayText.size()) &&
			    (jsonArrayText[cursor] == ']'))
			{
				return true;
			}

			while (cursor < jsonArrayText.size())
			{
				if (jsonArrayText[cursor] != '{')
				{
					return false;
				}

				const std::optional<std::size_t> objectEnd =
				    FindMatchingJsonToken(jsonArrayText, cursor, '{', '}');
				if (!objectEnd.has_value())
				{
					return false;
				}

				objectTexts.push_back(
				    jsonArrayText.substr(
				        cursor,
				        (*objectEnd - cursor) + 1U));

				cursor =
				    SkipJsonWhitespace(jsonArrayText, *objectEnd + 1U);
				if (cursor >= jsonArrayText.size())
				{
					return false;
				}
				if (jsonArrayText[cursor] == ',')
				{
					cursor = SkipJsonWhitespace(
					    jsonArrayText,
					    cursor + 1U);
					continue;
				}
				if (jsonArrayText[cursor] == ']')
				{
					return true;
				}
				return false;
			}

			return false;
		}

		[[nodiscard]] bool TryReadDouble(const std::string& jsonText, std::string_view key, double& outputValue)
		{
			return Common::JsonKeyValueParser::TryReadDouble(
			    jsonText,
			    key,
			    outputValue);
		}

		[[nodiscard]] bool TryReadBool(const std::string& jsonText, std::string_view key, bool& outputValue)
		{
			return Common::JsonKeyValueParser::TryReadBool(
			    jsonText,
			    key,
			    outputValue);
		}

		[[nodiscard]] bool TryReadInt(
		    const std::string& jsonText,
		    std::string_view key,
		    int& outputValue)
		{
			return Common::JsonKeyValueParser::TryReadInt(
			    jsonText,
			    key,
			    outputValue);
		}

		[[nodiscard]] std::optional<std::string> TryConvertWideToUtf8(
		    const std::wstring_view text)
		{
			if (text.empty())
			{
				return std::string{};
			}

			const int utf8Size = WideCharToMultiByte(
			    CP_UTF8,
			    WC_ERR_INVALID_CHARS,
			    text.data(),
			    static_cast<int>(text.size()),
			    nullptr,
			    0,
			    nullptr,
			    nullptr);
			if (utf8Size <= 0)
			{
				return std::nullopt;
			}

			std::string utf8Text(static_cast<std::size_t>(utf8Size), '\0');
			const int converted = WideCharToMultiByte(
			    CP_UTF8,
			    WC_ERR_INVALID_CHARS,
			    text.data(),
			    static_cast<int>(text.size()),
			    utf8Text.data(),
			    utf8Size,
			    nullptr,
			    nullptr);
			if (converted != utf8Size)
			{
				return std::nullopt;
			}

			return utf8Text;
		}

		[[nodiscard]] std::optional<std::wstring> TryConvertUtf8ToWide(
		    const std::string_view utf8Text)
		{
			if (utf8Text.empty())
			{
				return std::wstring{};
			}

			const int wideSize = MultiByteToWideChar(
			    CP_UTF8,
			    MB_ERR_INVALID_CHARS,
			    utf8Text.data(),
			    static_cast<int>(utf8Text.size()),
			    nullptr,
			    0);
			if (wideSize <= 0)
			{
				return std::nullopt;
			}

			std::wstring wideText(static_cast<std::size_t>(wideSize), L'\0');
			const int converted = MultiByteToWideChar(
			    CP_UTF8,
			    MB_ERR_INVALID_CHARS,
			    utf8Text.data(),
			    static_cast<int>(utf8Text.size()),
			    wideText.data(),
			    wideSize);
			if (converted != wideSize)
			{
				return std::nullopt;
			}

			return wideText;
		}

		[[nodiscard]] std::string BuildPrefixedKey(
		    std::string_view prefix,
		    std::string_view fieldName)
		{
			if (prefix.empty())
			{
				return std::string{ fieldName };
			}
			std::string key{ prefix };
			key += fieldName;
			return key;
		}

		[[nodiscard]] std::string BuildPrefixedCommentKey(
		    std::string_view prefix,
		    std::string_view fieldName)
		{
			std::string key{ "_comment_" };
			key += BuildPrefixedKey(prefix, fieldName);
			return key;
		}

		[[nodiscard]] std::string_view GetPitchParameterComment(
		    const std::string_view fieldName)
		{
			if (fieldName == "initialSpeedMetersPerSecond")
			{
				return "Initial speed [m/s], direction fixed toward home plate.";
			}
			if (fieldName == "releaseHeightMeters")
			{
				return "Pitch release height [m] before mound offset.";
			}
			if (fieldName == "spinAxisX")
			{
				return "Spin axis vector X component.";
			}
			if (fieldName == "spinAxisY")
			{
				return "Spin axis vector Y component.";
			}
			if (fieldName == "spinAxisZ")
			{
				return "Spin axis vector Z component.";
			}
			if (fieldName == "spinRpm")
			{
				return "Spin rate [rpm].";
			}
			if (fieldName == "dragCoefficient")
			{
				return "Drag coefficient.";
			}
			if (fieldName == "liftCoefficient")
			{
				return "Lift coefficient.";
			}
			if (fieldName == "useCoefficientTable")
			{
				return "Enable velocity/spin-dependent coefficient table.";
			}
			if (fieldName == "enableSeamPerturbation")
			{
				return "Enable seam perturbation.";
			}
			if (fieldName == "seamPerturbationAmplitude")
			{
				return "Seam perturbation amplitude.";
			}
			if (fieldName == "seamPerturbationFrequencyHz")
			{
				return "Seam perturbation frequency [Hz].";
			}
			if (fieldName == "seamPerturbationPhase")
			{
				return "Seam perturbation phase [rad].";
			}
			return "";
		}

		[[nodiscard]] bool TryReadPitchParametersWithPrefix(
		    const std::string& jsonText,
		    std::string_view prefix,
		    Baseball::PitchParameters& outputPitchParameters)
		{
			Baseball::PitchParameters parameters{};
			const std::string releaseHeightKey =
			    BuildPrefixedKey(prefix, "releaseHeightMeters");
			if (Common::JsonKeyValueParser::ExtractRawValue(
			        jsonText,
			        releaseHeightKey)
			        .has_value() &&
			    !TryReadDouble(
			        jsonText,
			        releaseHeightKey,
			        parameters.releaseHeightMeters))
			{
				return false;
			}
			double initialSpeedMetersPerSecond = 0.0;
			const std::string initialSpeedKey =
			    BuildPrefixedKey(prefix, "initialSpeedMetersPerSecond");
			if (Common::JsonKeyValueParser::ExtractRawValue(
			        jsonText,
			        initialSpeedKey)
			        .has_value())
			{
				if (!TryReadDouble(
				        jsonText,
				        initialSpeedKey,
				        initialSpeedMetersPerSecond))
				{
					return false;
				}
			}
			else
			{
				double legacyInitialVelocityX = 0.0;
				double legacyInitialVelocityY = 0.0;
				double legacyInitialVelocityZ = 0.0;
				if (!TryReadDouble(
				        jsonText,
				        BuildPrefixedKey(prefix, "initialVelocityX"),
				        legacyInitialVelocityX) ||
				    !TryReadDouble(
				        jsonText,
				        BuildPrefixedKey(prefix, "initialVelocityY"),
				        legacyInitialVelocityY) ||
				    !TryReadDouble(
				        jsonText,
				        BuildPrefixedKey(prefix, "initialVelocityZ"),
				        legacyInitialVelocityZ))
				{
					return false;
				}
				initialSpeedMetersPerSecond = std::sqrt(
				    (legacyInitialVelocityX * legacyInitialVelocityX) +
				    (legacyInitialVelocityY * legacyInitialVelocityY) +
				    (legacyInitialVelocityZ * legacyInitialVelocityZ));
			}
			parameters.initialVelocity = Physics::Vector3{
				0.0,
				0.0,
				std::abs(initialSpeedMetersPerSecond)
			};
			if (!TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "spinAxisX"),
			        parameters.spinAxis.x) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "spinAxisY"),
			        parameters.spinAxis.y) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "spinAxisZ"),
			        parameters.spinAxis.z) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "spinRpm"),
			        parameters.spinRpm) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "dragCoefficient"),
			        parameters.dragCoefficient) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "liftCoefficient"),
			        parameters.liftCoefficient) ||
			    !TryReadBool(
			        jsonText,
			        BuildPrefixedKey(prefix, "useCoefficientTable"),
			        parameters.useCoefficientTable) ||
			    !TryReadBool(
			        jsonText,
			        BuildPrefixedKey(prefix, "enableSeamPerturbation"),
			        parameters.enableSeamPerturbation) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "seamPerturbationAmplitude"),
			        parameters.seamPerturbationAmplitude) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "seamPerturbationFrequencyHz"),
			        parameters.seamPerturbationFrequencyHz) ||
			    !TryReadDouble(
			        jsonText,
			        BuildPrefixedKey(prefix, "seamPerturbationPhase"),
			        parameters.seamPerturbationPhase))
			{
				return false;
			}

			outputPitchParameters = parameters;
			return true;
		}

		void AppendPitchParameterEntries(
		    std::vector<std::string>* const entries,
		    std::string_view prefix,
		    const Baseball::PitchParameters& pitchParameters)
		{
			if (entries == nullptr)
			{
				return;
			}

			auto addNumber = [&](std::string_view key, const double value) -> void
			{
				std::ostringstream commentLine{};
				commentLine << "\""
				            << BuildPrefixedCommentKey(prefix, key)
				            << "\": \""
				            << GetPitchParameterComment(key)
				            << "\"";
				entries->push_back(commentLine.str());

				std::ostringstream line{};
				line.setf(std::ios::fixed);
				line.precision(std::numeric_limits<double>::max_digits10);
				line << "\"" << BuildPrefixedKey(prefix, key)
				     << "\": " << value;
				entries->push_back(line.str());
			};
			auto addBool = [&](std::string_view key, const bool value) -> void
			{
				std::ostringstream commentLine{};
				commentLine << "\""
				            << BuildPrefixedCommentKey(prefix, key)
				            << "\": \""
				            << GetPitchParameterComment(key)
				            << "\"";
				entries->push_back(commentLine.str());

				std::ostringstream line{};
				line << "\"" << BuildPrefixedKey(prefix, key) << "\": "
				     << (value ? "true" : "false");
				entries->push_back(line.str());
			};

			addNumber(
			    "initialSpeedMetersPerSecond",
			    std::abs(pitchParameters.initialVelocity.z));
			addNumber("releaseHeightMeters", pitchParameters.releaseHeightMeters);
			addNumber("spinAxisX", pitchParameters.spinAxis.x);
			addNumber("spinAxisY", pitchParameters.spinAxis.y);
			addNumber("spinAxisZ", pitchParameters.spinAxis.z);
			addNumber("spinRpm", pitchParameters.spinRpm);
			addNumber("dragCoefficient", pitchParameters.dragCoefficient);
			addNumber("liftCoefficient", pitchParameters.liftCoefficient);
			addBool("useCoefficientTable", pitchParameters.useCoefficientTable);
			addBool(
			    "enableSeamPerturbation",
			    pitchParameters.enableSeamPerturbation);
			addNumber(
			    "seamPerturbationAmplitude",
			    pitchParameters.seamPerturbationAmplitude);
			addNumber(
			    "seamPerturbationFrequencyHz",
			    pitchParameters.seamPerturbationFrequencyHz);
			addNumber(
			    "seamPerturbationPhase",
			    pitchParameters.seamPerturbationPhase);
		}
	} // namespace

	bool PitchPresetJsonStorage::SavePitchSettings(
	    std::wstring_view filePath,
	    const PitchRuntimeSettings& settings,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ofstream outputFile{ std::filesystem::path(filePath) };
		if (!outputFile.is_open())
		{
			errorMessage = L"Failed to open preset JSON file for writing.";
			return false;
		}

		std::vector<std::string> entries{};
		entries.reserve(
		    32U +
		    (settings.pitchProfiles.size() * 48U));
		AppendPitchParameterEntries(
		    &entries,
		    "",
		    settings.pitchParameters);

		std::string environmentFilePath{
			DefaultEnvironmentSettingsFileName
		};
		if (!settings.environmentSettingsFile.empty())
		{
			const std::optional<std::string> environmentFilePathUtf8 =
			    TryConvertWideToUtf8(settings.environmentSettingsFile);
			if (!environmentFilePathUtf8.has_value())
			{
				errorMessage =
				    L"Failed to convert environment settings file path to UTF-8.";
				return false;
			}
			environmentFilePath = *environmentFilePathUtf8;
		}
		entries.push_back(
		    "\"_comment_environmentSettingsFile\": \"Referenced environment settings JSON file name.\"");
		entries.push_back(
		    "\"environmentSettingsFile\": \"" +
		    EscapeJsonString(environmentFilePath) + "\"");

		entries.push_back(
		    "\"_comment_activePitchProfileIndex\": \"Selected pitch profile index when random selection is disabled.\"");
		{
			std::ostringstream indexEntry{};
			indexEntry << "\"activePitchProfileIndex\": "
			           << settings.activePitchProfileIndex;
			entries.push_back(indexEntry.str());
		}
		entries.push_back(
		    "\"_comment_randomPitchProfileEnabled\": \"If true, choose pitch profile randomly.\"");
		entries.push_back(
		    std::string{ "\"randomPitchProfileEnabled\": " } +
		    (settings.randomPitchProfileEnabled ? "true" : "false"));

		outputFile << "{\n";
		for (const std::string& entry : entries)
		{
			outputFile << "  " << entry << ",\n";
		}

		outputFile << "  \"_comment_pitchProfiles\": \"Array of pitch profiles and their pitch parameters.\",\n";
		outputFile << "  \"pitchProfiles\": [\n";
		for (std::size_t profileIndex = 0;
		     profileIndex < settings.pitchProfiles.size();
		     ++profileIndex)
		{
			const PitchProfileEntry& profile =
			    settings.pitchProfiles[profileIndex];
			std::string profileName{ "Pitch" };
			if (!profile.name.empty())
			{
				const std::optional<std::string> profileNameUtf8 =
				    TryConvertWideToUtf8(profile.name);
				if (!profileNameUtf8.has_value())
				{
					errorMessage =
					    L"Failed to convert pitch profile name to UTF-8.";
					return false;
				}
				profileName = *profileNameUtf8;
			}

			std::vector<std::string> profileEntries{};
			profileEntries.reserve(48U);
			profileEntries.push_back(
			    "\"_comment_name\": \"Pitch profile display name used in UI/logs.\"");
			profileEntries.push_back(
			    "\"name\": \"" + EscapeJsonString(profileName) + "\"");
			AppendPitchParameterEntries(
			    &profileEntries,
			    "",
			    profile.pitchParameters);

			outputFile << "    {\n";
			for (std::size_t entryIndex = 0;
			     entryIndex < profileEntries.size();
			     ++entryIndex)
			{
				outputFile << "      " << profileEntries[entryIndex];
				if (entryIndex + 1U < profileEntries.size())
				{
					outputFile << ",";
				}
				outputFile << "\n";
			}
			outputFile << "    }";
			if (profileIndex + 1U < settings.pitchProfiles.size())
			{
				outputFile << ",";
			}
			outputFile << "\n";
		}
		outputFile << "  ]\n";
		outputFile << "}\n";

		if (!outputFile.good())
		{
			errorMessage = L"Failed while writing preset JSON file.";
			return false;
		}

		return true;
	}

	std::optional<PitchRuntimeSettings> PitchPresetJsonStorage::LoadPitchSettings(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		errorMessage.clear();

		std::ifstream inputFile{ std::filesystem::path(filePath) };
		if (!inputFile.is_open())
		{
			errorMessage = L"Preset JSON file could not be opened.";
			return std::nullopt;
		}

		std::stringstream buffer{};
		buffer << inputFile.rdbuf();
		if (!inputFile.good() && !inputFile.eof())
		{
			errorMessage = L"Failed while reading preset JSON file.";
			return std::nullopt;
		}

		const std::string jsonText = buffer.str();
		PitchRuntimeSettings settings{};
		if (!TryReadPitchParametersWithPrefix(
		        jsonText,
		        "",
		        settings.pitchParameters))
		{
			errorMessage = L"Preset JSON format is invalid or missing required fields.";
			return std::nullopt;
		}

		const JsonStringLookupResult environmentSettingsFile =
		    FindJsonStringByKey(jsonText, "environmentSettingsFile");
		if (environmentSettingsFile.status == JsonStringLookupStatus::Invalid)
		{
			errorMessage =
			    L"Preset JSON environmentSettingsFile is not a valid JSON string.";
			return std::nullopt;
		}
		if ((environmentSettingsFile.status == JsonStringLookupStatus::Found) &&
		    !environmentSettingsFile.value.empty())
		{
			const std::optional<std::wstring> wideEnvironmentSettingsFile =
			    TryConvertUtf8ToWide(environmentSettingsFile.value);
			if (!wideEnvironmentSettingsFile.has_value())
			{
				errorMessage =
				    L"Preset JSON environmentSettingsFile is not valid UTF-8.";
				return std::nullopt;
			}
			settings.environmentSettingsFile = *wideEnvironmentSettingsFile;
		}
		else
		{
			settings.environmentSettingsFile =
			    L"EnvironmentSettings.user.json";
		}

		settings.activePitchProfileIndex = -1;
		(void)TryReadInt(
		    jsonText,
		    "activePitchProfileIndex",
		    settings.activePitchProfileIndex);
		settings.randomPitchProfileEnabled = true;
		(void)TryReadBool(
		    jsonText,
		    "randomPitchProfileEnabled",
		    settings.randomPitchProfileEnabled);

		const JsonArrayLookupResult profileArrayResult =
		    FindJsonArrayByKey(jsonText, "pitchProfiles");
		if (profileArrayResult.status == JsonArrayLookupStatus::Found)
		{
			std::vector<std::string_view> profileObjectTexts{};
			if (!TryExtractJsonObjectsFromArray(
			        profileArrayResult.value,
			        profileObjectTexts))
			{
				errorMessage =
				    L"Preset JSON pitchProfiles array format is invalid.";
				return std::nullopt;
			}

			settings.pitchProfiles.clear();
			settings.pitchProfiles.reserve(profileObjectTexts.size());
			for (const std::string_view profileObjectText : profileObjectTexts)
			{
				const std::string profileJson{ profileObjectText };
				const JsonStringLookupResult profileName =
				    FindJsonStringByKey(profileJson, "name");
				if (profileName.status == JsonStringLookupStatus::Invalid)
				{
					errorMessage =
					    L"Preset JSON pitch profile name is not a valid JSON string.";
					return std::nullopt;
				}
				if ((profileName.status != JsonStringLookupStatus::Found) ||
				    profileName.value.empty())
				{
					errorMessage =
					    L"Preset JSON pitch profile name is missing.";
					return std::nullopt;
				}

				PitchProfileEntry profile{};
				const std::optional<std::wstring> wideProfileName =
				    TryConvertUtf8ToWide(profileName.value);
				if (!wideProfileName.has_value())
				{
					errorMessage =
					    L"Preset JSON pitch profile name is not valid UTF-8.";
					return std::nullopt;
				}
				profile.name = *wideProfileName;
				if (!TryReadPitchParametersWithPrefix(
				        profileJson,
				        "",
				        profile.pitchParameters))
				{
					errorMessage =
					    L"Preset JSON pitch profile fields are invalid.";
					return std::nullopt;
				}

				settings.pitchProfiles.push_back(profile);
			}
		}
		else if (profileArrayResult.status == JsonArrayLookupStatus::Invalid)
		{
			errorMessage =
			    L"Preset JSON pitchProfiles array format is invalid.";
			return std::nullopt;
		}
		else
		{
			int pitchProfileCount = 0;
			if (TryReadInt(jsonText, "pitchProfileCount", pitchProfileCount) &&
			    (pitchProfileCount > 0))
			{
				settings.pitchProfiles.clear();
				settings.pitchProfiles.reserve(
				    static_cast<std::size_t>(pitchProfileCount));
				for (int profileIndex = 0;
				     profileIndex < pitchProfileCount;
				     ++profileIndex)
				{
					const std::string prefix =
					    "pitchProfile" + std::to_string(profileIndex);
					const JsonStringLookupResult profileName =
					    FindJsonStringByKey(jsonText, prefix + "Name");
					if (profileName.status == JsonStringLookupStatus::Invalid)
					{
						errorMessage =
						    L"Preset JSON pitch profile name is not a valid JSON string.";
						return std::nullopt;
					}
					if ((profileName.status != JsonStringLookupStatus::Found) ||
					    profileName.value.empty())
					{
						errorMessage =
						    L"Preset JSON pitch profile name is missing.";
						return std::nullopt;
					}

					PitchProfileEntry profile{};
					const std::optional<std::wstring> wideProfileName =
					    TryConvertUtf8ToWide(profileName.value);
					if (!wideProfileName.has_value())
					{
						errorMessage =
						    L"Preset JSON pitch profile name is not valid UTF-8.";
						return std::nullopt;
					}
					profile.name = *wideProfileName;
					if (!TryReadPitchParametersWithPrefix(
					        jsonText,
					        prefix,
					        profile.pitchParameters))
					{
						errorMessage =
						    L"Preset JSON pitch profile fields are invalid.";
						return std::nullopt;
					}

					settings.pitchProfiles.push_back(profile);
				}
			}
		}

		if (settings.pitchProfiles.empty())
		{
			PitchProfileEntry fallbackProfile{};
			fallbackProfile.name = L"Custom";
			fallbackProfile.pitchParameters = settings.pitchParameters;
			settings.pitchProfiles.push_back(fallbackProfile);
		}
		if (settings.activePitchProfileIndex < 0)
		{
			settings.activePitchProfileIndex = 0;
		}
		if (settings.activePitchProfileIndex >=
		    static_cast<int>(settings.pitchProfiles.size()))
		{
			settings.activePitchProfileIndex =
			    static_cast<int>(settings.pitchProfiles.size()) - 1;
		}

		return settings;
	}

	bool PitchPresetJsonStorage::Save(
	    std::wstring_view filePath,
	    const Baseball::PitchParameters& pitchParameters,
	    std::wstring& errorMessage) const
	{
		PitchRuntimeSettings settings{};
		settings.pitchParameters = pitchParameters;
		settings.environmentSettingsFile = L"EnvironmentSettings.user.json";
		settings.pitchProfiles = std::vector<PitchProfileEntry>{
			PitchProfileEntry{
			    L"Custom",
			    pitchParameters }
		};
		settings.activePitchProfileIndex = 0;
		settings.randomPitchProfileEnabled = false;
		return SavePitchSettings(filePath, settings, errorMessage);
	}

	std::optional<Baseball::PitchParameters> PitchPresetJsonStorage::Load(
	    std::wstring_view filePath,
	    std::wstring& errorMessage) const
	{
		const std::optional<PitchRuntimeSettings> settings =
		    LoadPitchSettings(filePath, errorMessage);
		if (!settings.has_value())
		{
			return std::nullopt;
		}
		return settings->pitchParameters;
	}
} // namespace BaseballSimulatorVR::Data

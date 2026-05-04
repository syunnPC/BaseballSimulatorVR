#pragma once

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Common::JsonKeyValueParser
{
	enum class KeyReadStatus
	{
		Missing,
		Invalid,
		Valid
	};

	template <typename TValue>
	struct KeyReadResult final
	{
		KeyReadStatus status{ KeyReadStatus::Missing };
		std::optional<TValue> value{};
	};

	[[nodiscard]] inline std::string TrimCopy(std::string value)
	{
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](const unsigned char character)
		                                        { return !std::isspace(character); }));
		value.erase(
		    std::find_if(value.rbegin(), value.rend(), [](const unsigned char character)
		                 { return !std::isspace(character); })
		        .base(),
		    value.end());
		return value;
	}

	[[nodiscard]] inline std::optional<std::uint32_t> DecodeHexDigit(
	    const char character)
	{
		if ((character >= '0') && (character <= '9'))
		{
			return static_cast<std::uint32_t>(character - '0');
		}
		if ((character >= 'a') && (character <= 'f'))
		{
			return 10U + static_cast<std::uint32_t>(character - 'a');
		}
		if ((character >= 'A') && (character <= 'F'))
		{
			return 10U + static_cast<std::uint32_t>(character - 'A');
		}
		return std::nullopt;
	}

	inline void AppendUtf8CodePoint(
	    std::string& output,
	    const std::uint32_t codePoint)
	{
		if (codePoint <= 0x7FU)
		{
			output.push_back(static_cast<char>(codePoint));
			return;
		}
		if (codePoint <= 0x7FFU)
		{
			output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
			output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
			return;
		}
		if (codePoint <= 0xFFFFU)
		{
			output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
			output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
			output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
			return;
		}

		output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
		output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
		output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
		output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
	}

	[[nodiscard]] inline std::optional<std::uint32_t> TryReadUnicodeEscape(
	    const std::string& jsonText,
	    std::size_t* const cursor)
	{
		if (cursor == nullptr)
		{
			return std::nullopt;
		}

		if ((*cursor + 4U) > jsonText.size())
		{
			return std::nullopt;
		}

		std::uint32_t codePoint = 0U;
		for (std::size_t index = 0; index < 4U; ++index)
		{
			const std::optional<std::uint32_t> hexValue =
			    DecodeHexDigit(jsonText[*cursor + index]);
			if (!hexValue.has_value())
			{
				return std::nullopt;
			}
			codePoint = (codePoint << 4U) | *hexValue;
		}
		*cursor += 4U;

		if ((codePoint < 0xD800U) || (codePoint > 0xDFFFU))
		{
			return codePoint;
		}
		if (codePoint > 0xDBFFU)
		{
			return std::nullopt;
		}
		if (((*cursor + 6U) > jsonText.size()) ||
		    (jsonText[*cursor] != '\\') ||
		    (jsonText[*cursor + 1U] != 'u'))
		{
			return std::nullopt;
		}

		*cursor += 2U;
		std::uint32_t lowSurrogate = 0U;
		for (std::size_t index = 0; index < 4U; ++index)
		{
			const std::optional<std::uint32_t> hexValue =
			    DecodeHexDigit(jsonText[*cursor + index]);
			if (!hexValue.has_value())
			{
				return std::nullopt;
			}
			lowSurrogate = (lowSurrogate << 4U) | *hexValue;
		}
		*cursor += 4U;
		if ((lowSurrogate < 0xDC00U) || (lowSurrogate > 0xDFFFU))
		{
			return std::nullopt;
		}

		return 0x10000U +
		       (((codePoint - 0xD800U) << 10U) |
		        (lowSurrogate - 0xDC00U));
	}

	[[nodiscard]] inline std::optional<std::string> ParseJsonStringLiteral(
	    const std::string& jsonText,
	    const std::size_t quoteIndex)
	{
		if ((quoteIndex >= jsonText.size()) || (jsonText[quoteIndex] != '"'))
		{
			return std::nullopt;
		}

		std::string value{};
		for (std::size_t cursor = quoteIndex + 1U;
		     cursor < jsonText.size();
		     ++cursor)
		{
			const char character = jsonText[cursor];
			if (character == '"')
			{
				return value;
			}
			if (character != '\\')
			{
				value.push_back(character);
				continue;
			}

			++cursor;
			if (cursor >= jsonText.size())
			{
				return std::nullopt;
			}

			switch (jsonText[cursor])
			{
				case '"':
				case '\\':
				case '/':
					value.push_back(jsonText[cursor]);
					break;
				case 'b':
					value.push_back('\b');
					break;
				case 'f':
					value.push_back('\f');
					break;
				case 'n':
					value.push_back('\n');
					break;
				case 'r':
					value.push_back('\r');
					break;
				case 't':
					value.push_back('\t');
					break;
				case 'u':
				{
					++cursor;
					const std::optional<std::uint32_t> codePoint =
					    TryReadUnicodeEscape(jsonText, &cursor);
					if (!codePoint.has_value())
					{
						return std::nullopt;
					}
					AppendUtf8CodePoint(value, *codePoint);
					--cursor;
					break;
				}
				default:
					return std::nullopt;
			}
		}

		return std::nullopt;
	}

	[[nodiscard]] inline std::optional<std::string> ExtractRawValue(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::string keyToken = "\"" + std::string(key) + "\"";
		const std::size_t keyPosition = jsonText.find(keyToken);
		if (keyPosition == std::string::npos)
		{
			return std::nullopt;
		}

		const std::size_t colonPosition = jsonText.find(':', keyPosition + keyToken.size());
		if (colonPosition == std::string::npos)
		{
			return std::nullopt;
		}

		std::size_t valueBegin = colonPosition + 1;
		while ((valueBegin < jsonText.size()) &&
		       std::isspace(static_cast<unsigned char>(jsonText[valueBegin])))
		{
			++valueBegin;
		}
		if (valueBegin >= jsonText.size())
		{
			return std::nullopt;
		}

		if (jsonText[valueBegin] == '"')
		{
			return ParseJsonStringLiteral(jsonText, valueBegin);
		}

		std::size_t valueEnd = valueBegin;
		while ((valueEnd < jsonText.size()) && (jsonText[valueEnd] != ',') &&
		       (jsonText[valueEnd] != '}'))
		{
			++valueEnd;
		}
		return TrimCopy(jsonText.substr(valueBegin, valueEnd - valueBegin));
	}

	[[nodiscard]] inline std::optional<double> ReadDouble(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return std::nullopt;
		}

		char* parseEnd = nullptr;
		errno = 0;
		const double parsedValue = std::strtod(rawValue->c_str(), &parseEnd);
		if ((parseEnd == rawValue->c_str()) ||
		    ((parseEnd != nullptr) && (*parseEnd != '\0')) ||
		    (errno == ERANGE) ||
		    !std::isfinite(parsedValue))
		{
			return std::nullopt;
		}

		return parsedValue;
	}

	[[nodiscard]] inline KeyReadResult<double> ReadDoubleResult(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		if (!ExtractRawValue(jsonText, key).has_value())
		{
			return KeyReadResult<double>{};
		}

		const std::optional<double> parsedValue = ReadDouble(jsonText, key);
		if (!parsedValue.has_value())
		{
			return KeyReadResult<double>{
				KeyReadStatus::Invalid,
				std::nullopt
			};
		}

		return KeyReadResult<double>{
			KeyReadStatus::Valid,
			parsedValue
		};
	}

	[[nodiscard]] inline bool TryReadDouble(
	    const std::string& jsonText,
	    const std::string_view key,
	    double& outputValue)
	{
		const std::optional<double> parsedValue = ReadDouble(jsonText, key);
		if (!parsedValue.has_value())
		{
			return false;
		}
		outputValue = *parsedValue;
		return true;
	}

	[[nodiscard]] inline std::optional<int> ReadInt(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return std::nullopt;
		}

		char* parseEnd = nullptr;
		errno = 0;
		const long parsedValue = std::strtol(rawValue->c_str(), &parseEnd, 10);
		if ((parseEnd == rawValue->c_str()) ||
		    ((parseEnd != nullptr) && (*parseEnd != '\0')) ||
		    (errno == ERANGE) ||
		    (parsedValue <
		     static_cast<long>((std::numeric_limits<int>::min)())) ||
		    (parsedValue >
		     static_cast<long>((std::numeric_limits<int>::max)())))
		{
			return std::nullopt;
		}

		return static_cast<int>(parsedValue);
	}

	[[nodiscard]] inline KeyReadResult<int> ReadIntResult(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		if (!ExtractRawValue(jsonText, key).has_value())
		{
			return KeyReadResult<int>{};
		}

		const std::optional<int> parsedValue = ReadInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return KeyReadResult<int>{
				KeyReadStatus::Invalid,
				std::nullopt
			};
		}

		return KeyReadResult<int>{
			KeyReadStatus::Valid,
			parsedValue
		};
	}

	[[nodiscard]] inline bool TryReadInt(
	    const std::string& jsonText,
	    const std::string_view key,
	    int& outputValue)
	{
		const std::optional<int> parsedValue = ReadInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return false;
		}
		outputValue = *parsedValue;
		return true;
	}

	[[nodiscard]] inline std::optional<std::uint32_t> ReadUnsignedInt(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return std::nullopt;
		}
		if (!rawValue->empty() &&
		    ((*rawValue)[0] == '-' || (*rawValue)[0] == '+'))
		{
			return std::nullopt;
		}

		char* parseEnd = nullptr;
		errno = 0;
		const unsigned long parsedValue = std::strtoul(rawValue->c_str(), &parseEnd, 10);
		if ((parseEnd == rawValue->c_str()) ||
		    ((parseEnd != nullptr) && (*parseEnd != '\0')) ||
		    (errno == ERANGE) ||
		    (parsedValue > (std::numeric_limits<std::uint32_t>::max)()))
		{
			return std::nullopt;
		}

		return static_cast<std::uint32_t>(parsedValue);
	}

	[[nodiscard]] inline KeyReadResult<std::uint32_t> ReadUnsignedIntResult(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		if (!ExtractRawValue(jsonText, key).has_value())
		{
			return KeyReadResult<std::uint32_t>{};
		}

		const std::optional<std::uint32_t> parsedValue =
		    ReadUnsignedInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return KeyReadResult<std::uint32_t>{
				KeyReadStatus::Invalid,
				std::nullopt
			};
		}

		return KeyReadResult<std::uint32_t>{
			KeyReadStatus::Valid,
			parsedValue
		};
	}

	[[nodiscard]] inline bool TryReadUnsignedInt(
	    const std::string& jsonText,
	    const std::string_view key,
	    std::uint32_t& outputValue)
	{
		const std::optional<std::uint32_t> parsedValue =
		    ReadUnsignedInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return false;
		}
		outputValue = *parsedValue;
		return true;
	}

	[[nodiscard]] inline std::optional<int> ReadOptionalInt(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value() || (*rawValue == "null"))
		{
			return std::nullopt;
		}
		return ReadInt(jsonText, key);
	}

	[[nodiscard]] inline KeyReadResult<std::optional<int>> ReadOptionalIntResult(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return KeyReadResult<std::optional<int>>{};
		}

		if (*rawValue == "null")
		{
			return KeyReadResult<std::optional<int>>{
				KeyReadStatus::Valid,
				std::optional<int>{}
			};
		}

		const std::optional<int> parsedValue = ReadInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return KeyReadResult<std::optional<int>>{
				KeyReadStatus::Invalid,
				std::nullopt
			};
		}

		return KeyReadResult<std::optional<int>>{
			KeyReadStatus::Valid,
			parsedValue
		};
	}

	[[nodiscard]] inline bool TryReadOptionalInt(
	    const std::string& jsonText,
	    const std::string_view key,
	    std::optional<int>& outputValue)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return false;
		}
		if (*rawValue == "null")
		{
			outputValue.reset();
			return true;
		}

		const std::optional<int> parsedValue = ReadInt(jsonText, key);
		if (!parsedValue.has_value())
		{
			return false;
		}
		outputValue = *parsedValue;
		return true;
	}

	[[nodiscard]] inline std::optional<bool> ReadBool(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		const std::optional<std::string> rawValue = ExtractRawValue(jsonText, key);
		if (!rawValue.has_value())
		{
			return std::nullopt;
		}
		if (*rawValue == "true")
		{
			return true;
		}
		if (*rawValue == "false")
		{
			return false;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline KeyReadResult<bool> ReadBoolResult(
	    const std::string& jsonText,
	    const std::string_view key)
	{
		if (!ExtractRawValue(jsonText, key).has_value())
		{
			return KeyReadResult<bool>{};
		}

		const std::optional<bool> parsedValue = ReadBool(jsonText, key);
		if (!parsedValue.has_value())
		{
			return KeyReadResult<bool>{
				KeyReadStatus::Invalid,
				std::nullopt
			};
		}

		return KeyReadResult<bool>{
			KeyReadStatus::Valid,
			parsedValue
		};
	}

	[[nodiscard]] inline bool TryReadBool(
	    const std::string& jsonText,
	    const std::string_view key,
	    bool& outputValue)
	{
		const std::optional<bool> parsedValue = ReadBool(jsonText, key);
		if (!parsedValue.has_value())
		{
			return false;
		}
		outputValue = *parsedValue;
		return true;
	}
} // namespace BaseballSimulatorVR::Common::JsonKeyValueParser

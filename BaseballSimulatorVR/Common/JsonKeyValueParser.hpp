#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BaseballSimulatorVR::Common::JsonKeyValueParser
{
	enum class KeyReadStatus
	{
		Missing,
		Invalid,
		Valid
	};

	template <typename TValue> struct KeyReadResult final
	{
		KeyReadStatus status{ KeyReadStatus::Missing };
		std::optional<TValue> value{};
	};

	enum class ValueType
	{
		String,
		Number,
		Boolean,
		Null,
		Object,
		Array
	};

	// Raw spans refer to the input document; decoded strings own their contents.
	struct JsonValue final
	{
		ValueType type{ ValueType::Null };
		std::string_view raw{};
		std::string stringValue{};
	};

	namespace Detail
	{
		class Parser final
		{
		  public:
			explicit Parser(const std::string_view text) : text_(text)
			{
			}

			[[nodiscard]] KeyReadResult<JsonValue> ReadMember(const std::string_view key)
			{
				key_ = key;
				SkipWhitespace();
				if (!ReadObject(0U, true))
					return { KeyReadStatus::Invalid, std::nullopt };
				SkipWhitespace();
				if (cursor_ != text_.size())
					return { KeyReadStatus::Invalid, std::nullopt };
				return std::move(member_);
			}

			[[nodiscard]] bool ReadObjectArray(std::vector<std::string_view>& objects)
			{
				SkipWhitespace();
				if (!Consume('['))
					return false;
				SkipWhitespace();
				if (!Consume(']'))
				{
					do
					{
						SkipWhitespace();
						const std::size_t begin = cursor_;
						if (!ReadObject(0U, false))
							return false;
						objects.push_back(text_.substr(begin, cursor_ - begin));
						SkipWhitespace();
						if (Consume(']'))
							break;
						if (!Consume(','))
							return false;
					} while (true);
				}
				SkipWhitespace();
				return cursor_ == text_.size();
			}

		  private:
			// Bound recursive descent so settings cannot exhaust the stack.
			static constexpr std::size_t MaximumDepth = 128U;
			std::string_view text_;
			std::size_t cursor_{ 0U };
			std::string_view key_{};
			KeyReadResult<JsonValue> member_{};

			void SkipWhitespace()
			{
				while (cursor_ < text_.size())
				{
					const char c = text_[cursor_];
					if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
						break;
					++cursor_;
				}
			}

			[[nodiscard]] bool Consume(const char c)
			{
				if (cursor_ == text_.size() || text_[cursor_] != c)
					return false;
				++cursor_;
				return true;
			}

			[[nodiscard]] bool ReadHex(std::uint32_t& value)
			{
				if (text_.size() - cursor_ < 4U)
					return false;
				value = 0U;
				for (std::size_t i = 0U; i < 4U; ++i)
				{
					const char c = text_[cursor_++];
					std::uint32_t digit = 0U;
					if (c >= '0' && c <= '9')
						digit = static_cast<std::uint32_t>(c - '0');
					else if (c >= 'a' && c <= 'f')
						digit = 10U + static_cast<std::uint32_t>(c - 'a');
					else if (c >= 'A' && c <= 'F')
						digit = 10U + static_cast<std::uint32_t>(c - 'A');
					else
						return false;
					value = (value << 4U) | digit;
				}
				return true;
			}

			static void AppendCodePoint(std::string& output, const std::uint32_t point)
			{
				if (point <= 0x7FU)
					output.push_back(static_cast<char>(point));
				else
				{
					if (point <= 0x7FFU)
						output.push_back(static_cast<char>(0xC0U | (point >> 6U)));
					else
					{
						if (point <= 0xFFFFU)
							output.push_back(static_cast<char>(0xE0U | (point >> 12U)));
						else
						{
							output.push_back(static_cast<char>(0xF0U | (point >> 18U)));
							output.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3FU)));
						}
						output.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
					}
					output.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
				}
			}

			[[nodiscard]] bool ReadUtf8(std::string& output)
			{
				const std::size_t begin = cursor_;
				const auto first = static_cast<unsigned char>(text_[cursor_++]);
				std::size_t remaining = 0U;
				std::uint32_t point = 0U;
				std::uint32_t minimum = 0U;
				if (first >= 0xC2U && first <= 0xDFU)
				{
					remaining = 1U;
					point = first & 0x1FU;
					minimum = 0x80U;
				}
				else if (first >= 0xE0U && first <= 0xEFU)
				{
					remaining = 2U;
					point = first & 0x0FU;
					minimum = 0x800U;
				}
				else if (first >= 0xF0U && first <= 0xF4U)
				{
					remaining = 3U;
					point = first & 0x07U;
					minimum = 0x10000U;
				}
				else
					return false;
				if (text_.size() - cursor_ < remaining)
					return false;
				for (std::size_t i = 0U; i < remaining; ++i)
				{
					const auto next = static_cast<unsigned char>(text_[cursor_++]);
					if ((next & 0xC0U) != 0x80U)
						return false;
					point = (point << 6U) | (next & 0x3FU);
				}
				if (point < minimum || point > 0x10FFFFU || (point >= 0xD800U && point <= 0xDFFFU))
					return false;
				output.append(text_.substr(begin, cursor_ - begin));
				return true;
			}

			[[nodiscard]] bool ReadString(std::string& output)
			{
				if (!Consume('"'))
					return false;
				while (cursor_ < text_.size())
				{
					const auto c = static_cast<unsigned char>(text_[cursor_]);
					if (c >= 0x80U)
					{
						if (!ReadUtf8(output))
							return false;
						continue;
					}
					++cursor_;
					if (c == '"')
						return true;
					if (c < 0x20U)
						return false;
					if (c != '\\')
					{
						output.push_back(static_cast<char>(c));
						continue;
					}
					if (cursor_ == text_.size())
						return false;
					const char escape = text_[cursor_++];
					switch (escape)
					{
					case '"':
					case '\\':
					case '/':
						output.push_back(escape);
						break;
					case 'b':
						output.push_back('\b');
						break;
					case 'f':
						output.push_back('\f');
						break;
					case 'n':
						output.push_back('\n');
						break;
					case 'r':
						output.push_back('\r');
						break;
					case 't':
						output.push_back('\t');
						break;
					case 'u':
					{
						std::uint32_t point = 0U;
						if (!ReadHex(point))
							return false;
						if (point >= 0xD800U && point <= 0xDBFFU)
						{
							std::uint32_t low = 0U;
							if (!Consume('\\') || !Consume('u') || !ReadHex(low) || low < 0xDC00U ||
							    low > 0xDFFFU)
								return false;
							point = 0x10000U + ((point - 0xD800U) << 10U) + (low - 0xDC00U);
						}
						else if (point >= 0xDC00U && point <= 0xDFFFU)
							return false;
						AppendCodePoint(output, point);
						break;
					}
					default:
						return false;
					}
				}
				return false;
			}

			[[nodiscard]] bool IsDigit() const
			{
				return cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9';
			}

			[[nodiscard]] bool ReadNumber()
			{
				(void)Consume('-');
				if (!Consume('0'))
				{
					if (!IsDigit())
						return false;
					while (IsDigit())
						++cursor_;
				}
				if (Consume('.'))
				{
					if (!IsDigit())
						return false;
					while (IsDigit())
						++cursor_;
				}
				if (Consume('e') || Consume('E'))
				{
					if (!Consume('+'))
						(void)Consume('-');
					if (!IsDigit())
						return false;
					while (IsDigit())
						++cursor_;
				}
				return true;
			}

			[[nodiscard]] bool ReadLiteral(const std::string_view literal)
			{
				if (text_.substr(cursor_, literal.size()) != literal)
					return false;
				cursor_ += literal.size();
				return true;
			}

			[[nodiscard]] bool ReadValue(const std::size_t depth, JsonValue& value)
			{
				SkipWhitespace();
				if (cursor_ == text_.size() || depth >= MaximumDepth)
					return false;
				const std::size_t begin = cursor_;
				switch (text_[cursor_])
				{
				case '"':
					value.type = ValueType::String;
					if (!ReadString(value.stringValue))
						return false;
					break;
				case '{':
					value.type = ValueType::Object;
					if (!ReadObject(depth, false))
						return false;
					break;
				case '[':
					value.type = ValueType::Array;
					if (!ReadArray(depth))
						return false;
					break;
				case 't':
				case 'f':
					value.type = ValueType::Boolean;
					if (!ReadLiteral(text_[cursor_] == 't' ? "true" : "false"))
						return false;
					break;
				case 'n':
					value.type = ValueType::Null;
					if (!ReadLiteral("null"))
						return false;
					break;
				default:
					value.type = ValueType::Number;
					if (!ReadNumber())
						return false;
					break;
				}
				value.raw = text_.substr(begin, cursor_ - begin);
				return true;
			}

			[[nodiscard]] bool ReadObject(const std::size_t depth, const bool lookup)
			{
				if (depth >= MaximumDepth || !Consume('{'))
					return false;
				SkipWhitespace();
				if (Consume('}'))
					return true;
				std::unordered_set<std::string> keys{};
				do
				{
					SkipWhitespace();
					std::string name{};
					if (!ReadString(name) || !keys.insert(name).second)
						return false;
					SkipWhitespace();
					if (!Consume(':'))
						return false;
					JsonValue value{};
					if (!ReadValue(depth + 1U, value))
						return false;
					if (lookup && name == key_)
						member_ = { KeyReadStatus::Valid, std::move(value) };
					SkipWhitespace();
					if (Consume('}'))
						return true;
					if (!Consume(','))
						return false;
				} while (true);
			}

			[[nodiscard]] bool ReadArray(const std::size_t depth)
			{
				if (!Consume('['))
					return false;
				SkipWhitespace();
				if (Consume(']'))
					return true;
				do
				{
					JsonValue value{};
					if (!ReadValue(depth + 1U, value))
						return false;
					SkipWhitespace();
					if (Consume(']'))
						return true;
					if (!Consume(','))
						return false;
				} while (true);
			}
		};

		template <typename TValue, typename TConvert>
		[[nodiscard]] KeyReadResult<TValue> ReadTyped(
		    const std::string_view text,
		    const std::string_view key,
		    const ValueType type,
		    TConvert convert)
		{
			const auto member = Parser(text).ReadMember(key);
			if (member.status != KeyReadStatus::Valid)
				return { member.status, std::nullopt };
			if (member.value->type != type)
				return { KeyReadStatus::Invalid, std::nullopt };
			const std::optional<TValue> value = convert(*member.value);
			return { value.has_value() ? KeyReadStatus::Valid : KeyReadStatus::Invalid, value };
		}

		template <typename TValue>
		[[nodiscard]] std::optional<TValue> ConvertNumber(const JsonValue& value)
		{
			TValue number{};
			const auto result =
			    std::from_chars(value.raw.data(), value.raw.data() + value.raw.size(), number);
			if (result.ec != std::errc{} || result.ptr != value.raw.data() + value.raw.size())
				return std::nullopt;
			return number;
		}

		template <typename TValue>
		[[nodiscard]] bool AssignResult(const KeyReadResult<TValue>& result, TValue& output)
		{
			if (result.status != KeyReadStatus::Valid)
				return false;
			output = *result.value;
			return true;
		}
	} // namespace Detail

	[[nodiscard]] inline KeyReadResult<JsonValue> ReadValueResult(
	    const std::string_view text, const std::string_view key)
	{
		return Detail::Parser(text).ReadMember(key);
	}

	[[nodiscard]] inline std::optional<std::string> ExtractRawValue(
	    const std::string& text, const std::string_view key)
	{
		const auto result = ReadValueResult(text, key);
		if (result.status != KeyReadStatus::Valid)
			return std::nullopt;
		return result.value->type == ValueType::String ? result.value->stringValue
		                                               : std::string(result.value->raw);
	}

	[[nodiscard]] inline KeyReadResult<std::string> ReadStringResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<std::string>(
		    text, key, ValueType::String,
		    [](const JsonValue& value) -> std::optional<std::string> { return value.stringValue; });
	}

	[[nodiscard]] inline KeyReadResult<std::vector<std::string_view>> ReadObjectArrayResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<std::vector<std::string_view>>(
		    text, key, ValueType::Array,
		    [](const JsonValue& value) -> std::optional<std::vector<std::string_view>>
		    {
			    std::vector<std::string_view> objects{};
			    if (!Detail::Parser(value.raw).ReadObjectArray(objects))
				    return std::nullopt;
			    return objects;
		    });
	}

	[[nodiscard]] inline KeyReadResult<double> ReadDoubleResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<double>(
		    text, key, ValueType::Number,
		    [](const JsonValue& value) -> std::optional<double>
		    {
			    const auto number = Detail::ConvertNumber<double>(value);
			    return number.has_value() && std::isfinite(*number) ? number : std::nullopt;
		    });
	}

	[[nodiscard]] inline KeyReadResult<int> ReadIntResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<int>(text, key, ValueType::Number, Detail::ConvertNumber<int>);
	}

	[[nodiscard]] inline KeyReadResult<std::uint32_t> ReadUnsignedIntResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<std::uint32_t>(
		    text, key, ValueType::Number, Detail::ConvertNumber<std::uint32_t>);
	}

	[[nodiscard]] inline KeyReadResult<bool> ReadBoolResult(
	    const std::string& text, const std::string_view key)
	{
		return Detail::ReadTyped<bool>(
		    text, key, ValueType::Boolean,
		    [](const JsonValue& value) -> std::optional<bool> { return value.raw == "true"; });
	}

	[[nodiscard]] inline KeyReadResult<std::optional<int>> ReadOptionalIntResult(
	    const std::string& text, const std::string_view key)
	{
		const auto member = ReadValueResult(text, key);
		if (member.status != KeyReadStatus::Valid)
			return { member.status, std::nullopt };
		if (member.value->type == ValueType::Null)
		{
			return { KeyReadStatus::Valid,
				     std::optional<std::optional<int>>(std::in_place, std::nullopt) };
		}
		if (member.value->type != ValueType::Number)
			return { KeyReadStatus::Invalid, std::nullopt };
		const auto number = Detail::ConvertNumber<int>(*member.value);
		if (!number.has_value())
			return { KeyReadStatus::Invalid, std::nullopt };
		return { KeyReadStatus::Valid, std::optional<std::optional<int>>(std::in_place, number) };
	}

	[[nodiscard]] inline std::optional<double> ReadDouble(
	    const std::string& text, const std::string_view key)
	{
		return ReadDoubleResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<int> ReadInt(
	    const std::string& text, const std::string_view key)
	{
		return ReadIntResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<std::uint32_t> ReadUnsignedInt(
	    const std::string& text, const std::string_view key)
	{
		return ReadUnsignedIntResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<bool> ReadBool(
	    const std::string& text, const std::string_view key)
	{
		return ReadBoolResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<int> ReadOptionalInt(
	    const std::string& text, const std::string_view key)
	{
		return ReadOptionalIntResult(text, key).value.value_or(std::nullopt);
	}
	[[nodiscard]] inline bool TryReadDouble(
	    const std::string& text, const std::string_view key, double& output)
	{
		return Detail::AssignResult(ReadDoubleResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadInt(
	    const std::string& text, const std::string_view key, int& output)
	{
		return Detail::AssignResult(ReadIntResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadUnsignedInt(
	    const std::string& text, const std::string_view key, std::uint32_t& output)
	{
		return Detail::AssignResult(ReadUnsignedIntResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadBool(
	    const std::string& text, const std::string_view key, bool& output)
	{
		return Detail::AssignResult(ReadBoolResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadOptionalInt(
	    const std::string& text, const std::string_view key, std::optional<int>& output)
	{
		return Detail::AssignResult(ReadOptionalIntResult(text, key), output);
	}
} // namespace BaseballSimulatorVR::Common::JsonKeyValueParser

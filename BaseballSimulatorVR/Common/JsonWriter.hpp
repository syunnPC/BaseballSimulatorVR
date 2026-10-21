#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Minimal locale-independent JSON writer for settings files.
namespace BaseballSimulatorVR::Common::JsonWriter
{
	[[nodiscard]] inline std::string EscapeString(const std::string_view value)
	{
		constexpr char HexDigits[] = "0123456789ABCDEF";
		std::string escaped{};
		escaped.reserve(value.size() + 2U);
		escaped.push_back('"');
		for (const char character : value)
		{
			const auto code = static_cast<unsigned char>(character);
			switch (character)
			{
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\b':
				escaped += "\\b";
				break;
			case '\f':
				escaped += "\\f";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				if (code < 0x20U)
				{
					escaped += "\\u00";
					escaped.push_back(HexDigits[(code >> 4U) & 0x0FU]);
					escaped.push_back(HexDigits[code & 0x0FU]);
				}
				else
				{
					escaped.push_back(character);
				}
				break;
			}
		}
		escaped.push_back('"');
		return escaped;
	}

	// Shortest text that parses back to exactly the same double (std::to_chars without a
	// precision), so 18.44 stays "18.44" instead of "18.44000000000000128". Integral values keep
	// a ".0" suffix to read as real numbers. Non-finite values cannot be represented in JSON.
	[[nodiscard]] inline std::optional<std::string> FormatNumber(const double value)
	{
		if (!std::isfinite(value))
		{
			return std::nullopt;
		}
		char buffer[64]{};
		const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
		if (error != std::errc{})
		{
			return std::nullopt;
		}
		std::string text(buffer, end);
		if (text.find_first_of(".eE") == std::string::npos)
		{
			text += ".0";
		}
		return text;
	}

	template <typename TInteger> [[nodiscard]] std::string FormatInteger(const TInteger value)
	{
		char buffer[32]{};
		const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
		return error == std::errc{} ? std::string(buffer, end) : std::string{ "0" };
	}

	// Builds a pretty-printed object with two-space indentation, preserving member order.
	class ObjectBuilder final
	{
	  public:
		void AddRaw(const std::string_view key, std::string rawJsonValue)
		{
			m_members.emplace_back(std::string{ key }, std::move(rawJsonValue));
		}

		// Documentation member: "_comment_<key>": "text". Readers ignore unknown members.
		void AddComment(const std::string_view key, const std::string_view text)
		{
			if (!text.empty())
			{
				AddRaw("_comment_" + std::string{ key }, EscapeString(text));
			}
		}

		void AddString(const std::string_view key, const std::string_view utf8Value)
		{
			AddRaw(key, EscapeString(utf8Value));
		}

		[[nodiscard]] bool AddNumber(const std::string_view key, const double value)
		{
			const std::optional<std::string> text = FormatNumber(value);
			if (!text.has_value())
			{
				m_failed = true;
				return false;
			}
			AddRaw(key, *text);
			return true;
		}

		template <typename TInteger> void AddInteger(const std::string_view key, const TInteger value)
		{
			AddRaw(key, FormatInteger(value));
		}

		void AddBool(const std::string_view key, const bool value)
		{
			AddRaw(key, value ? "true" : "false");
		}

		void AddNull(const std::string_view key)
		{
			AddRaw(key, "null");
		}

		void AddObjectArray(const std::string_view key, const std::vector<ObjectBuilder>& objects)
		{
			if (objects.empty())
			{
				AddRaw(key, "[]");
				return;
			}
			std::string text = "[\n";
			for (std::size_t index = 0U; index < objects.size(); ++index)
			{
				m_failed = m_failed || objects[index].HasFailed();
				text += Indent(objects[index].Build(), "  ");
				text += index + 1U < objects.size() ? ",\n" : "\n";
			}
			text += "]";
			AddRaw(key, std::move(text));
		}

		// True when a value (for example NaN) could not be represented.
		[[nodiscard]] bool HasFailed() const noexcept
		{
			return m_failed;
		}

		[[nodiscard]] std::string Build() const
		{
			if (m_members.empty())
			{
				return "{}";
			}
			std::string text = "{\n";
			for (std::size_t index = 0U; index < m_members.size(); ++index)
			{
				std::string member = EscapeString(m_members[index].first) + ": " + m_members[index].second;
				text += Indent(member, "  ");
				text += index + 1U < m_members.size() ? ",\n" : "\n";
			}
			text += "}";
			return text;
		}

		// Complete document text with a trailing newline.
		[[nodiscard]] std::string BuildDocument() const
		{
			return Build() + "\n";
		}

	  private:
		// String values never contain raw line breaks (they are escaped), so line splitting is safe.
		[[nodiscard]] static std::string Indent(const std::string_view text, const std::string_view prefix)
		{
			std::string indented{};
			indented.reserve(text.size() + prefix.size() * 4U);
			bool lineStart = true;
			for (const char character : text)
			{
				if (lineStart)
				{
					indented += prefix;
					lineStart = false;
				}
				indented.push_back(character);
				if (character == '\n')
				{
					lineStart = true;
				}
			}
			return indented;
		}

		std::vector<std::pair<std::string, std::string>> m_members{};
		bool m_failed{ false };
	};
} // namespace BaseballSimulatorVR::Common::JsonWriter

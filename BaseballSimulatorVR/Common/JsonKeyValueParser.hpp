#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

// Strict JSON reader for settings files. A document is parsed once into an immutable DOM which is
// then queried by key. Parsing enforces RFC 8259 grammar, UTF-8 and surrogate validity, unique
// member names and a nesting limit; numbers are converted locale-independently with from_chars.
// A leading UTF-8 byte order mark (as written by Notepad "UTF-8 with BOM") is ignored.
namespace BaseballSimulatorVR::Common::JsonKeyValueParser
{
	enum class KeyReadStatus
	{
		Missing,
		Invalid,
		Valid
	};

	// Location and reason of a parse or type error. line/column are 1-based (column counts
	// Unicode code points); 0 means "unknown". path is the dotted member path, for example
	// "pitchProfiles[2].spinRpm".
	struct JsonDiagnostic final
	{
		std::size_t line{ 0U };
		std::size_t column{ 0U };
		std::string path{};
		std::string message{};

		[[nodiscard]] bool HasPosition() const noexcept
		{
			return line != 0U;
		}
	};

	// "line 3, column 7, key \"spinRpm\": expected a number" (UTF-8).
	[[nodiscard]] inline std::string FormatDiagnostic(const JsonDiagnostic& diagnostic)
	{
		std::string text{};
		if (diagnostic.HasPosition())
		{
			text += "line " + std::to_string(diagnostic.line) + ", column " + std::to_string(diagnostic.column);
		}
		if (!diagnostic.path.empty())
		{
			if (!text.empty())
			{
				text += ", ";
			}
			text += "key \"" + diagnostic.path + "\"";
		}
		if (!text.empty())
		{
			text += ": ";
		}
		text += diagnostic.message.empty() ? std::string{ "invalid value" } : diagnostic.message;
		return text;
	}

	template <typename TValue> struct KeyReadResult final
	{
		KeyReadStatus status{ KeyReadStatus::Missing };
		std::optional<TValue> value{};
		// Describes the problem when status is Invalid.
		JsonDiagnostic diagnostic{};
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

	// Raw spans refer to the parsed document text; decoded strings own their contents.
	struct JsonValue final
	{
		ValueType type{ ValueType::Null };
		std::string_view raw{};
		std::string stringValue{};
		std::size_t line{ 0U };
		std::size_t column{ 0U };
	};

	class JsonObjectView;

	namespace Detail
	{
		// Bound recursive descent so settings cannot exhaust the stack.
		inline constexpr std::size_t MaximumDepth = 128U;
		inline constexpr std::string_view Utf8ByteOrderMark = "\xEF\xBB\xBF";

		struct Node final
		{
			ValueType type{ ValueType::Null };
			std::size_t begin{ 0U };
			std::size_t length{ 0U };
			std::string stringValue{};
			std::vector<std::size_t> children{};
			std::vector<std::string> keys{};
		};

		struct DocumentData final
		{
			std::string text{};
			std::vector<Node> nodes{};
			bool valid{ false };
			JsonDiagnostic error{};

			[[nodiscard]] std::pair<std::size_t, std::size_t> LineColumn(const std::size_t offset) const
			{
				std::size_t line = 1U;
				std::size_t column = 1U;
				std::size_t index = text.starts_with(Utf8ByteOrderMark) ? Utf8ByteOrderMark.size() : 0U;
				const std::size_t end = offset < text.size() ? offset : text.size();
				for (; index < end; ++index)
				{
					const auto c = static_cast<unsigned char>(text[index]);
					if (c == '\n')
					{
						++line;
						column = 1U;
					}
					else if ((c & 0xC0U) != 0x80U)
					{
						++column;
					}
				}
				return { line, column };
			}

			[[nodiscard]] JsonDiagnostic MakeDiagnostic(
			    const std::size_t offset, std::string path, std::string message) const
			{
				const auto [line, column] = LineColumn(offset);
				return JsonDiagnostic{ line, column, std::move(path), std::move(message) };
			}
		};

		class Parser final
		{
		  public:
			explicit Parser(DocumentData& document) : m_document(document), m_text(document.text)
			{
			}

			void Parse()
			{
				if (m_text.starts_with(Utf8ByteOrderMark))
				{
					m_cursor = Utf8ByteOrderMark.size();
				}
				SkipWhitespace();
				if (m_cursor == m_text.size())
				{
					Fail("the document is empty; a JSON object is required");
					return;
				}
				if (m_text[m_cursor] != '{')
				{
					Fail("the document root must be a JSON object");
					return;
				}
				const std::size_t root = ParseValue(0U);
				if (root == InvalidNode)
				{
					return;
				}
				SkipWhitespace();
				if (m_cursor != m_text.size())
				{
					Fail("unexpected content after the end of the JSON document");
					return;
				}
				m_document.valid = true;
			}

		  private:
			static constexpr std::size_t InvalidNode = static_cast<std::size_t>(-1);

			DocumentData& m_document;
			std::string_view m_text;
			std::size_t m_cursor{ 0U };
			std::vector<std::string> m_path{};
			bool m_failed{ false };

			[[nodiscard]] std::string BuildPath() const
			{
				std::string path{};
				for (const std::string& segment : m_path)
				{
					if (!path.empty() && !segment.starts_with('['))
					{
						path += '.';
					}
					path += segment;
				}
				return path;
			}

			void FailAt(const std::size_t offset, std::string message)
			{
				if (m_failed)
				{
					return;
				}
				m_failed = true;
				m_document.valid = false;
				m_document.error = m_document.MakeDiagnostic(offset, BuildPath(), std::move(message));
			}

			void Fail(std::string message)
			{
				FailAt(m_cursor, std::move(message));
			}

			void SkipWhitespace()
			{
				while (m_cursor < m_text.size())
				{
					const char c = m_text[m_cursor];
					if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
					{
						break;
					}
					++m_cursor;
				}
			}

			[[nodiscard]] bool Consume(const char c)
			{
				if (m_cursor == m_text.size() || m_text[m_cursor] != c)
				{
					return false;
				}
				++m_cursor;
				return true;
			}

			[[nodiscard]] bool ReadHex(std::uint32_t& value)
			{
				if (m_text.size() - m_cursor < 4U)
				{
					Fail("a \\u escape requires four hexadecimal digits");
					return false;
				}
				value = 0U;
				for (std::size_t i = 0U; i < 4U; ++i)
				{
					const char c = m_text[m_cursor];
					std::uint32_t digit = 0U;
					if (c >= '0' && c <= '9')
						digit = static_cast<std::uint32_t>(c - '0');
					else if (c >= 'a' && c <= 'f')
						digit = 10U + static_cast<std::uint32_t>(c - 'a');
					else if (c >= 'A' && c <= 'F')
						digit = 10U + static_cast<std::uint32_t>(c - 'A');
					else
					{
						Fail("a \\u escape requires four hexadecimal digits");
						return false;
					}
					++m_cursor;
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
				const std::size_t begin = m_cursor;
				const auto first = static_cast<unsigned char>(m_text[m_cursor]);
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
				{
					Fail("invalid UTF-8 byte sequence (save the file as UTF-8)");
					return false;
				}
				if (m_text.size() - m_cursor - 1U < remaining)
				{
					Fail("truncated UTF-8 byte sequence");
					return false;
				}
				for (std::size_t i = 1U; i <= remaining; ++i)
				{
					const auto next = static_cast<unsigned char>(m_text[m_cursor + i]);
					if ((next & 0xC0U) != 0x80U)
					{
						Fail("invalid UTF-8 byte sequence (save the file as UTF-8)");
						return false;
					}
					point = (point << 6U) | (next & 0x3FU);
				}
				if (point < minimum || point > 0x10FFFFU || (point >= 0xD800U && point <= 0xDFFFU))
				{
					Fail("invalid UTF-8 byte sequence (save the file as UTF-8)");
					return false;
				}
				m_cursor += remaining + 1U;
				output.append(m_text.substr(begin, m_cursor - begin));
				return true;
			}

			[[nodiscard]] bool ReadString(std::string& output)
			{
				const std::size_t begin = m_cursor;
				if (!Consume('"'))
				{
					Fail("expected a string");
					return false;
				}
				while (m_cursor < m_text.size())
				{
					const auto c = static_cast<unsigned char>(m_text[m_cursor]);
					if (c >= 0x80U)
					{
						if (!ReadUtf8(output))
							return false;
						continue;
					}
					if (c == '"')
					{
						++m_cursor;
						return true;
					}
					if (c < 0x20U)
					{
						Fail("control characters such as line breaks must be escaped inside strings");
						return false;
					}
					++m_cursor;
					if (c != '\\')
					{
						output.push_back(static_cast<char>(c));
						continue;
					}
					if (m_cursor == m_text.size())
						break;
					const char escape = m_text[m_cursor];
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
						++m_cursor;
						std::uint32_t point = 0U;
						if (!ReadHex(point))
							return false;
						if (point >= 0xD800U && point <= 0xDBFFU)
						{
							std::uint32_t low = 0U;
							if (!Consume('\\') || !Consume('u'))
							{
								Fail("a UTF-16 high surrogate escape must be followed by a low surrogate");
								return false;
							}
							if (!ReadHex(low))
								return false;
							if (low < 0xDC00U || low > 0xDFFFU)
							{
								Fail("a UTF-16 high surrogate escape must be followed by a low surrogate");
								return false;
							}
							point = 0x10000U + ((point - 0xD800U) << 10U) + (low - 0xDC00U);
						}
						else if (point >= 0xDC00U && point <= 0xDFFFU)
						{
							Fail("unpaired UTF-16 low surrogate escape");
							return false;
						}
						AppendCodePoint(output, point);
						continue;
					}
					default:
						Fail("invalid escape sequence in string");
						return false;
					}
					++m_cursor;
				}
				FailAt(begin, "unterminated string");
				return false;
			}

			[[nodiscard]] bool IsDigit() const
			{
				return m_cursor < m_text.size() && m_text[m_cursor] >= '0' && m_text[m_cursor] <= '9';
			}

			[[nodiscard]] bool ReadNumber()
			{
				const std::size_t begin = m_cursor;
				(void)Consume('-');
				if (!Consume('0'))
				{
					if (!IsDigit())
					{
						FailAt(begin, "invalid value (expected a number, string, true, false, null, object or array)");
						return false;
					}
					while (IsDigit())
						++m_cursor;
				}
				if (Consume('.'))
				{
					if (!IsDigit())
					{
						FailAt(begin, "invalid number: digits are required after the decimal point");
						return false;
					}
					while (IsDigit())
						++m_cursor;
				}
				if (Consume('e') || Consume('E'))
				{
					if (!Consume('+'))
						(void)Consume('-');
					if (!IsDigit())
					{
						FailAt(begin, "invalid number: digits are required in the exponent");
						return false;
					}
					while (IsDigit())
						++m_cursor;
				}
				if (m_cursor < m_text.size())
				{
					const char next = m_text[m_cursor];
					if ((next >= '0' && next <= '9') || next == '.' || next == 'x' || next == 'X' ||
					    (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'))
					{
						FailAt(begin, "invalid number (leading zeros, hexadecimal and named numbers are not allowed)");
						return false;
					}
				}
				return true;
			}

			[[nodiscard]] bool ReadLiteral(const std::string_view literal)
			{
				if (m_text.substr(m_cursor, literal.size()) != literal)
				{
					Fail("invalid literal (expected true, false or null)");
					return false;
				}
				m_cursor += literal.size();
				if (m_cursor < m_text.size())
				{
					const char next = m_text[m_cursor];
					if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9'))
					{
						Fail("invalid literal (expected true, false or null)");
						return false;
					}
				}
				return true;
			}

			[[nodiscard]] std::size_t ParseValue(const std::size_t depth)
			{
				SkipWhitespace();
				if (m_cursor == m_text.size())
				{
					Fail("unexpected end of document (a value is missing)");
					return InvalidNode;
				}
				if (depth >= MaximumDepth)
				{
					Fail("values are nested more than 128 levels deep");
					return InvalidNode;
				}
				const std::size_t index = m_document.nodes.size();
				m_document.nodes.emplace_back();
				const std::size_t begin = m_cursor;
				ValueType type = ValueType::Null;
				bool succeeded = false;
				switch (m_text[m_cursor])
				{
				case '"':
				{
					type = ValueType::String;
					std::string decoded{};
					succeeded = ReadString(decoded);
					m_document.nodes[index].stringValue = std::move(decoded);
					break;
				}
				case '{':
					type = ValueType::Object;
					succeeded = ReadObject(depth, index);
					break;
				case '[':
					type = ValueType::Array;
					succeeded = ReadArray(depth, index);
					break;
				case 't':
				case 'f':
					type = ValueType::Boolean;
					succeeded = ReadLiteral(m_text[m_cursor] == 't' ? "true" : "false");
					break;
				case 'n':
					type = ValueType::Null;
					succeeded = ReadLiteral("null");
					break;
				default:
					type = ValueType::Number;
					succeeded = ReadNumber();
					break;
				}
				if (!succeeded)
				{
					return InvalidNode;
				}
				Node& node = m_document.nodes[index];
				node.type = type;
				node.begin = begin;
				node.length = m_cursor - begin;
				return index;
			}

			[[nodiscard]] bool ReadObject(const std::size_t depth, const std::size_t index)
			{
				++m_cursor;
				SkipWhitespace();
				if (Consume('}'))
					return true;
				std::unordered_set<std::string> keys{};
				for (;;)
				{
					SkipWhitespace();
					if (m_cursor == m_text.size() || m_text[m_cursor] != '"')
					{
						Fail(m_cursor < m_text.size() && m_text[m_cursor] == '}'
						        ? "a trailing comma is not allowed before '}'"
						        : "expected a quoted member name");
						return false;
					}
					const std::size_t keyBegin = m_cursor;
					std::string name{};
					if (!ReadString(name))
						return false;
					m_path.push_back(name);
					if (!keys.insert(name).second)
					{
						FailAt(keyBegin, "duplicate member name");
						return false;
					}
					SkipWhitespace();
					if (!Consume(':'))
					{
						Fail("expected ':' after the member name");
						return false;
					}
					const std::size_t child = ParseValue(depth + 1U);
					if (child == InvalidNode)
						return false;
					m_path.pop_back();
					m_document.nodes[index].children.push_back(child);
					m_document.nodes[index].keys.push_back(std::move(name));
					SkipWhitespace();
					if (Consume('}'))
						return true;
					if (!Consume(','))
					{
						Fail(m_cursor == m_text.size() ? "unexpected end of document (missing '}')"
						                               : "expected ',' or '}' after an object member");
						return false;
					}
				}
			}

			[[nodiscard]] bool ReadArray(const std::size_t depth, const std::size_t index)
			{
				++m_cursor;
				SkipWhitespace();
				if (Consume(']'))
					return true;
				for (std::size_t element = 0U;; ++element)
				{
					SkipWhitespace();
					if (m_cursor < m_text.size() && m_text[m_cursor] == ']')
					{
						Fail("a trailing comma is not allowed before ']'");
						return false;
					}
					m_path.push_back("[" + std::to_string(element) + "]");
					const std::size_t child = ParseValue(depth + 1U);
					if (child == InvalidNode)
						return false;
					m_path.pop_back();
					m_document.nodes[index].children.push_back(child);
					SkipWhitespace();
					if (Consume(']'))
						return true;
					if (!Consume(','))
					{
						Fail(m_cursor == m_text.size() ? "unexpected end of document (missing ']')"
						                               : "expected ',' or ']' after an array element");
						return false;
					}
				}
			}
		};

		template <typename TValue>
		[[nodiscard]] std::optional<TValue> ConvertNumber(const std::string_view raw)
		{
			TValue number{};
			const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), number);
			if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size())
				return std::nullopt;
			return number;
		}

		template <typename TValue>
		[[nodiscard]] KeyReadResult<TValue> Invalid(JsonDiagnostic diagnostic)
		{
			return KeyReadResult<TValue>{ KeyReadStatus::Invalid, std::nullopt, std::move(diagnostic) };
		}

		template <typename TValue> [[nodiscard]] KeyReadResult<TValue> Missing()
		{
			return KeyReadResult<TValue>{ KeyReadStatus::Missing, std::nullopt, JsonDiagnostic{} };
		}

		template <typename TValue> [[nodiscard]] KeyReadResult<TValue> Valid(TValue value)
		{
			return KeyReadResult<TValue>{ KeyReadStatus::Valid, std::optional<TValue>(std::in_place, std::move(value)),
				                          JsonDiagnostic{} };
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

	// A view of one JSON object inside a parsed document. Views share ownership of the document,
	// so they stay valid after the JsonDocument they came from is destroyed.
	class JsonObjectView final
	{
	  public:
		JsonObjectView() = default;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_document != nullptr && m_document->valid && m_node < m_document->nodes.size() &&
			       m_document->nodes[m_node].type == ValueType::Object;
		}

		// Reason the view is unusable (parse error of the whole document).
		[[nodiscard]] JsonDiagnostic Error() const
		{
			if (m_document == nullptr)
				return JsonDiagnostic{ 0U, 0U, {}, "no JSON document" };
			if (!m_document->valid)
				return m_document->error;
			return {};
		}

		[[nodiscard]] const std::string& Path() const noexcept
		{
			return m_path;
		}

		[[nodiscard]] bool Contains(const std::string_view key) const
		{
			return FindChild(key).has_value();
		}

		[[nodiscard]] std::vector<std::string> Keys() const
		{
			return IsValid() ? m_document->nodes[m_node].keys : std::vector<std::string>{};
		}

		[[nodiscard]] std::string BuildChildPath(const std::string_view key) const
		{
			return m_path.empty() ? std::string{ key } : m_path + "." + std::string{ key };
		}

		// Diagnostic pointing at the member (or at this object when the member is absent).
		[[nodiscard]] JsonDiagnostic DiagnosticFor(const std::string_view key, std::string message) const
		{
			if (!IsValid())
				return Error();
			const std::optional<std::size_t> child = FindChild(key);
			const std::size_t offset = m_document->nodes[child.value_or(m_node)].begin;
			return m_document->MakeDiagnostic(offset, BuildChildPath(key), std::move(message));
		}

		[[nodiscard]] KeyReadResult<JsonValue> ReadValue(const std::string_view key) const
		{
			if (!IsValid())
				return Detail::Invalid<JsonValue>(Error());
			const std::optional<std::size_t> child = FindChild(key);
			if (!child)
				return Detail::Missing<JsonValue>();
			const Detail::Node& node = m_document->nodes[*child];
			const auto [line, column] = m_document->LineColumn(node.begin);
			return Detail::Valid(JsonValue{ node.type, std::string_view{ m_document->text }.substr(node.begin, node.length),
				node.stringValue, line, column });
		}

		[[nodiscard]] KeyReadResult<double> ReadDouble(const std::string_view key) const
		{
			return ReadNumber<double>(key, "expected a finite number",
			    [](const std::string_view raw) -> std::optional<double>
			    {
				    const auto number = Detail::ConvertNumber<double>(raw);
				    return number.has_value() && std::isfinite(*number) ? number : std::nullopt;
			    });
		}

		[[nodiscard]] KeyReadResult<int> ReadInt(const std::string_view key) const
		{
			return ReadNumber<int>(key, "expected an integer between -2147483648 and 2147483647",
			    [](const std::string_view raw) { return Detail::ConvertNumber<int>(raw); });
		}

		[[nodiscard]] KeyReadResult<std::uint32_t> ReadUnsignedInt(const std::string_view key) const
		{
			return ReadNumber<std::uint32_t>(key, "expected an unsigned integer between 0 and 4294967295",
			    [](const std::string_view raw) { return Detail::ConvertNumber<std::uint32_t>(raw); });
		}

		[[nodiscard]] KeyReadResult<bool> ReadBool(const std::string_view key) const
		{
			const auto node = ReadTypedNode<bool>(key, ValueType::Boolean, "expected true or false");
			if (!node.first)
				return node.second;
			return Detail::Valid(RawOf(*node.first) == "true");
		}

		[[nodiscard]] KeyReadResult<std::string> ReadString(const std::string_view key) const
		{
			const auto node = ReadTypedNode<std::string>(key, ValueType::String, "expected a string in double quotes");
			if (!node.first)
				return node.second;
			return Detail::Valid(node.first->stringValue);
		}

		[[nodiscard]] KeyReadResult<std::optional<int>> ReadOptionalInt(const std::string_view key) const
		{
			if (!IsValid())
				return Detail::Invalid<std::optional<int>>(Error());
			const std::optional<std::size_t> child = FindChild(key);
			if (!child)
				return Detail::Missing<std::optional<int>>();
			const Detail::Node& node = m_document->nodes[*child];
			if (node.type == ValueType::Null)
				return Detail::Valid(std::optional<int>{});
			const auto number = node.type == ValueType::Number ? Detail::ConvertNumber<int>(RawOf(node)) : std::nullopt;
			if (!number)
				return Detail::Invalid<std::optional<int>>(DiagnosticFor(key, "expected an integer or null"));
			return Detail::Valid(std::optional<int>{ *number });
		}

		[[nodiscard]] KeyReadResult<JsonObjectView> ReadObject(const std::string_view key) const
		{
			const auto node = ReadTypedNode<JsonObjectView>(key, ValueType::Object, "expected an object");
			if (!node.first)
				return node.second;
			return Detail::Valid(JsonObjectView{ m_document, *FindChild(key), BuildChildPath(key) });
		}

		[[nodiscard]] KeyReadResult<std::vector<JsonObjectView>> ReadObjectArray(const std::string_view key) const
		{
			using Result = std::vector<JsonObjectView>;
			const auto node = ReadTypedNode<Result>(key, ValueType::Array, "expected an array of objects");
			if (!node.first)
				return node.second;
			Result objects{};
			objects.reserve(node.first->children.size());
			for (std::size_t element = 0U; element < node.first->children.size(); ++element)
			{
				const std::size_t childIndex = node.first->children[element];
				const std::string path = BuildChildPath(key) + "[" + std::to_string(element) + "]";
				if (m_document->nodes[childIndex].type != ValueType::Object)
				{
					return Detail::Invalid<Result>(m_document->MakeDiagnostic(
					    m_document->nodes[childIndex].begin, path, "expected an object"));
				}
				objects.push_back(JsonObjectView{ m_document, childIndex, path });
			}
			return Detail::Valid(std::move(objects));
		}

		// Byte span of this object inside the document text.
		[[nodiscard]] std::pair<std::size_t, std::size_t> Span() const noexcept
		{
			if (!IsValid())
				return { 0U, 0U };
			return { m_document->nodes[m_node].begin, m_document->nodes[m_node].length };
		}

	  private:
		friend class JsonDocument;

		JsonObjectView(std::shared_ptr<const Detail::DocumentData> document, const std::size_t node, std::string path)
		    : m_document(std::move(document)), m_node(node), m_path(std::move(path))
		{
		}

		[[nodiscard]] std::optional<std::size_t> FindChild(const std::string_view key) const
		{
			if (!IsValid())
				return std::nullopt;
			const Detail::Node& node = m_document->nodes[m_node];
			for (std::size_t index = 0U; index < node.keys.size(); ++index)
			{
				if (node.keys[index] == key)
					return node.children[index];
			}
			return std::nullopt;
		}

		[[nodiscard]] std::string_view RawOf(const Detail::Node& node) const
		{
			return std::string_view{ m_document->text }.substr(node.begin, node.length);
		}

		template <typename TValue>
		[[nodiscard]] std::pair<const Detail::Node*, KeyReadResult<TValue>> ReadTypedNode(
		    const std::string_view key, const ValueType type, const char* const typeMessage) const
		{
			if (!IsValid())
				return { nullptr, Detail::Invalid<TValue>(Error()) };
			const std::optional<std::size_t> child = FindChild(key);
			if (!child)
				return { nullptr, Detail::Missing<TValue>() };
			const Detail::Node& node = m_document->nodes[*child];
			if (node.type != type)
				return { nullptr, Detail::Invalid<TValue>(DiagnosticFor(key, typeMessage)) };
			return { &node, KeyReadResult<TValue>{} };
		}

		template <typename TValue, typename TConvert>
		[[nodiscard]] KeyReadResult<TValue> ReadNumber(
		    const std::string_view key, const char* const rangeMessage, TConvert convert) const
		{
			const auto node = ReadTypedNode<TValue>(key, ValueType::Number, "expected a number (without quotes)");
			if (!node.first)
				return node.second;
			const std::optional<TValue> value = convert(RawOf(*node.first));
			if (!value)
				return Detail::Invalid<TValue>(DiagnosticFor(key, rangeMessage));
			return Detail::Valid(*value);
		}

		std::shared_ptr<const Detail::DocumentData> m_document{};
		std::size_t m_node{ 0U };
		std::string m_path{};
	};

	class JsonDocument final
	{
	  public:
		// Copies the text, so the document does not depend on the caller's buffer.
		[[nodiscard]] static JsonDocument Parse(const std::string_view text)
		{
			auto data = std::make_shared<Detail::DocumentData>();
			data->text.assign(text);
			Detail::Parser(*data).Parse();
			JsonDocument document{};
			document.m_data = std::move(data);
			return document;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_data != nullptr && m_data->valid;
		}

		[[nodiscard]] JsonDiagnostic Error() const
		{
			return m_data == nullptr ? JsonDiagnostic{ 0U, 0U, {}, "no JSON document" } : m_data->error;
		}

		[[nodiscard]] JsonObjectView Root() const
		{
			return JsonObjectView{ m_data, 0U, std::string{} };
		}

		[[nodiscard]] std::string_view Text() const noexcept
		{
			return m_data == nullptr ? std::string_view{} : std::string_view{ m_data->text };
		}

	  private:
		std::shared_ptr<const Detail::DocumentData> m_data{};
	};

	// Convenience wrappers that parse the text for a single lookup. Prefer parsing once with
	// JsonDocument::Parse when several keys are read from the same document. Raw spans returned by
	// these wrappers refer to the caller's text.
	[[nodiscard]] inline KeyReadResult<JsonValue> ReadValueResult(const std::string_view text, const std::string_view key)
	{
		const JsonDocument document = JsonDocument::Parse(text);
		KeyReadResult<JsonValue> result = document.Root().ReadValue(key);
		if (result.value)
		{
			const auto offset = static_cast<std::size_t>(result.value->raw.data() - document.Text().data());
			result.value->raw = text.substr(offset, result.value->raw.size());
		}
		return result;
	}

	[[nodiscard]] inline std::optional<std::string> ExtractRawValue(const std::string& text, const std::string_view key)
	{
		const auto result = ReadValueResult(text, key);
		if (result.status != KeyReadStatus::Valid)
			return std::nullopt;
		return result.value->type == ValueType::String ? result.value->stringValue : std::string(result.value->raw);
	}

	[[nodiscard]] inline KeyReadResult<std::string> ReadStringResult(const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadString(key);
	}

	[[nodiscard]] inline KeyReadResult<std::vector<std::string_view>> ReadObjectArrayResult(
	    const std::string& text, const std::string_view key)
	{
		const JsonDocument document = JsonDocument::Parse(text);
		const auto objects = document.Root().ReadObjectArray(key);
		if (objects.status != KeyReadStatus::Valid)
			return KeyReadResult<std::vector<std::string_view>>{ objects.status, std::nullopt, objects.diagnostic };
		std::vector<std::string_view> spans{};
		spans.reserve(objects.value->size());
		for (const JsonObjectView& object : *objects.value)
		{
			const auto [begin, length] = object.Span();
			spans.push_back(std::string_view{ text }.substr(begin, length));
		}
		return Detail::Valid(std::move(spans));
	}

	[[nodiscard]] inline KeyReadResult<double> ReadDoubleResult(const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadDouble(key);
	}

	[[nodiscard]] inline KeyReadResult<int> ReadIntResult(const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadInt(key);
	}

	[[nodiscard]] inline KeyReadResult<std::uint32_t> ReadUnsignedIntResult(
	    const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadUnsignedInt(key);
	}

	[[nodiscard]] inline KeyReadResult<bool> ReadBoolResult(const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadBool(key);
	}

	[[nodiscard]] inline KeyReadResult<std::optional<int>> ReadOptionalIntResult(
	    const std::string& text, const std::string_view key)
	{
		return JsonDocument::Parse(text).Root().ReadOptionalInt(key);
	}

	[[nodiscard]] inline std::optional<double> ReadDouble(const std::string& text, const std::string_view key)
	{
		return ReadDoubleResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<int> ReadInt(const std::string& text, const std::string_view key)
	{
		return ReadIntResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<std::uint32_t> ReadUnsignedInt(const std::string& text, const std::string_view key)
	{
		return ReadUnsignedIntResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<bool> ReadBool(const std::string& text, const std::string_view key)
	{
		return ReadBoolResult(text, key).value;
	}
	[[nodiscard]] inline std::optional<int> ReadOptionalInt(const std::string& text, const std::string_view key)
	{
		return ReadOptionalIntResult(text, key).value.value_or(std::nullopt);
	}
	[[nodiscard]] inline bool TryReadDouble(const std::string& text, const std::string_view key, double& output)
	{
		return Detail::AssignResult(ReadDoubleResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadInt(const std::string& text, const std::string_view key, int& output)
	{
		return Detail::AssignResult(ReadIntResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadUnsignedInt(const std::string& text, const std::string_view key, std::uint32_t& output)
	{
		return Detail::AssignResult(ReadUnsignedIntResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadBool(const std::string& text, const std::string_view key, bool& output)
	{
		return Detail::AssignResult(ReadBoolResult(text, key), output);
	}
	[[nodiscard]] inline bool TryReadOptionalInt(
	    const std::string& text, const std::string_view key, std::optional<int>& output)
	{
		return Detail::AssignResult(ReadOptionalIntResult(text, key), output);
	}
} // namespace BaseballSimulatorVR::Common::JsonKeyValueParser

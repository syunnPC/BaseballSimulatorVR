#pragma once

#include "../Common/AtomicFileWriter.hpp"
#include "../Common/JsonKeyValueParser.hpp"
#include "../Common/JsonWriter.hpp"
#include "../Common/TextConversion.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace BaseballSimulatorVR::Data::JsonSettings
{
	namespace Json = Common::JsonKeyValueParser;

	[[nodiscard]] inline std::wstring DescribeDiagnostic(const Json::JsonDiagnostic& diagnostic)
	{
		return Common::TextConversion::Utf8ToWideLossy(Json::FormatDiagnostic(diagnostic));
	}

	// Reads and parses a settings file once. On failure errorMessage names the problem, including
	// the line, column and member path of syntax errors.
	[[nodiscard]] inline std::optional<Json::JsonDocument> ReadDocument(
	    const std::wstring_view filePath, const std::wstring_view summary, std::wstring& errorMessage)
	{
		std::wstring readError{};
		const std::optional<std::string> text = Common::ReadWholeFile(std::filesystem::path{ filePath }, readError);
		if (!text.has_value())
		{
			errorMessage = std::wstring{ summary } + L" could not be read: " + readError;
			return std::nullopt;
		}
		Json::JsonDocument document = Json::JsonDocument::Parse(*text);
		if (!document.IsValid())
		{
			errorMessage = std::wstring{ summary } + L" is not valid JSON: " + DescribeDiagnostic(document.Error());
			return std::nullopt;
		}
		return document;
	}

	// Typed member access that reports the first error with its position and key path.
	class MemberReader final
	{
	  public:
		MemberReader(Json::JsonObjectView object, std::wstring summary, std::wstring& errorMessage)
		    : m_object(std::move(object)), m_summary(std::move(summary)), m_errorMessage(errorMessage)
		{
		}

		[[nodiscard]] const Json::JsonObjectView& Object() const noexcept
		{
			return m_object;
		}

		[[nodiscard]] bool Double(const std::string_view key, double& output, const bool required)
		{
			return Apply(m_object.ReadDouble(key), key, output, required);
		}

		[[nodiscard]] bool Int(const std::string_view key, int& output, const bool required)
		{
			return Apply(m_object.ReadInt(key), key, output, required);
		}

		[[nodiscard]] bool UnsignedInt(const std::string_view key, std::uint32_t& output, const bool required)
		{
			return Apply(m_object.ReadUnsignedInt(key), key, output, required);
		}

		[[nodiscard]] bool Bool(const std::string_view key, bool& output, const bool required)
		{
			return Apply(m_object.ReadBool(key), key, output, required);
		}

		[[nodiscard]] bool String(const std::string_view key, std::string& output, const bool required)
		{
			return Apply(m_object.ReadString(key), key, output, required);
		}

		[[nodiscard]] bool WideString(const std::string_view key, std::wstring& output, const bool required)
		{
			std::string utf8{};
			const bool present = m_object.Contains(key);
			if (!String(key, utf8, required))
			{
				return false;
			}
			if (!present)
			{
				return true;
			}
			const std::optional<std::wstring> wide = Common::TextConversion::TryUtf8ToWide(utf8);
			if (!wide.has_value())
			{
				return Fail(key, "the string is not valid UTF-8");
			}
			output = *wide;
			return true;
		}

		[[nodiscard]] bool OptionalInt(const std::string_view key, std::optional<int>& output, const bool required)
		{
			return Apply(m_object.ReadOptionalInt(key), key, output, required);
		}

		[[nodiscard]] bool Range(const std::string_view key, const double value, const double minimum, const double maximum)
		{
			if (value >= minimum && value <= maximum)
			{
				return true;
			}
			return Fail(key, "value " + Format(value) + " is outside the allowed range " + Format(minimum) +
			                     " to " + Format(maximum));
		}

		// Records an error located at the member (or at the object when it is absent).
		[[nodiscard]] bool Fail(const std::string_view key, std::string message)
		{
			if (m_errorMessage.empty())
			{
				m_errorMessage = m_summary + L": " + DescribeDiagnostic(m_object.DiagnosticFor(key, std::move(message)));
			}
			return false;
		}

	  private:
		[[nodiscard]] static std::string Format(const double value)
		{
			return Common::JsonWriter::FormatNumber(value).value_or(std::string{ "non-finite" });
		}

		template <typename TValue>
		[[nodiscard]] bool Apply(const Json::KeyReadResult<TValue>& result, const std::string_view key, TValue& output,
		    const bool required)
		{
			if (result.status == Json::KeyReadStatus::Valid)
			{
				output = *result.value;
				return true;
			}
			if (result.status == Json::KeyReadStatus::Missing)
			{
				return !required || Fail(key, "this required setting is missing");
			}
			if (m_errorMessage.empty())
			{
				m_errorMessage = m_summary + L": " + DescribeDiagnostic(result.diagnostic);
			}
			return false;
		}

		Json::JsonObjectView m_object;
		std::wstring m_summary;
		std::wstring& m_errorMessage;
	};

	// Accepts any well-formed JSON object; used to decide whether a file may become the ".bak".
	[[nodiscard]] inline bool IsJsonObjectDocument(const std::string_view text)
	{
		return Json::JsonDocument::Parse(text).IsValid();
	}

	[[nodiscard]] inline Common::AtomicWriteOptions SettingsWriteOptions()
	{
		Common::AtomicWriteOptions options{};
		options.keepBackup = true;
		options.isBackupContentValid = IsJsonObjectDocument;
		return options;
	}
} // namespace BaseballSimulatorVR::Data::JsonSettings

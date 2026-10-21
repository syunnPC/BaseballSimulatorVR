#pragma once

#include "../Framework.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace BaseballSimulatorVR::Common::TextConversion
{
	// Strict conversions: malformed input yields std::nullopt instead of replacement characters.
	[[nodiscard]] inline std::optional<std::string> TryWideToUtf8(const std::wstring_view text)
	{
		if (text.empty())
		{
			return std::string{};
		}
		if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
		{
			return std::nullopt;
		}

		const int length = static_cast<int>(text.size());
		const int utf8Size = WideCharToMultiByte(
		    CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
		if (utf8Size <= 0)
		{
			return std::nullopt;
		}

		std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, utf8.data(),
		        utf8Size, nullptr, nullptr) != utf8Size)
		{
			return std::nullopt;
		}
		return utf8;
	}

	[[nodiscard]] inline std::optional<std::wstring> TryMultiByteToWide(
	    const std::string_view text,
	    const UINT codePage,
	    const DWORD flags)
	{
		if (text.empty())
		{
			return std::wstring{};
		}
		if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
		{
			return std::nullopt;
		}

		const int length = static_cast<int>(text.size());
		const int wideSize = MultiByteToWideChar(codePage, flags, text.data(), length, nullptr, 0);
		if (wideSize <= 0)
		{
			return std::nullopt;
		}

		std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
		if (MultiByteToWideChar(codePage, flags, text.data(), length, wide.data(), wideSize) !=
		    wideSize)
		{
			return std::nullopt;
		}
		return wide;
	}

	[[nodiscard]] inline std::optional<std::wstring> TryUtf8ToWide(const std::string_view text)
	{
		return TryMultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
	}

	// Lenient conversion for diagnostics: invalid sequences become U+FFFD.
	[[nodiscard]] inline std::wstring Utf8ToWideLossy(const std::string_view text)
	{
		return TryMultiByteToWide(text, CP_UTF8, 0).value_or(std::wstring{});
	}

	// Text produced by the C runtime or std::error_code::message() uses the ANSI code page.
	[[nodiscard]] inline std::wstring AnsiToWide(const std::string_view text)
	{
		return TryMultiByteToWide(text, CP_ACP, 0).value_or(std::wstring{});
	}

	[[nodiscard]] inline std::wstring TrimTrailingSeparators(std::wstring text)
	{
		while (!text.empty() &&
		    (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'.'))
		{
			text.pop_back();
		}
		return text;
	}

	[[nodiscard]] inline std::wstring FormatSystemErrorMessage(const DWORD errorCode)
	{
		LPWSTR buffer = nullptr;
		const DWORD length = FormatMessageW(
		    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		        FORMAT_MESSAGE_IGNORE_INSERTS,
		    nullptr, errorCode, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
		std::wstring message{};
		if ((length != 0U) && (buffer != nullptr))
		{
			message.assign(buffer, length);
		}
		if (buffer != nullptr)
		{
			LocalFree(buffer);
		}
		message = TrimTrailingSeparators(std::move(message));
		const std::wstring code = L"Windows error " + std::to_wstring(errorCode);
		return message.empty() ? code : message + L" (" + code + L")";
	}

	// std::error_code::message() returns ANSI text; converting it byte-wise garbles
	// localized (for example Japanese) messages, so system errors are formatted natively.
	[[nodiscard]] inline std::wstring DescribeErrorCode(const std::error_code& error)
	{
		if (!error)
		{
			return L"no error";
		}
		if (error.category() == std::system_category())
		{
			return FormatSystemErrorMessage(static_cast<DWORD>(error.value()));
		}
		return TrimTrailingSeparators(AnsiToWide(error.message())) + L" (" +
		       AnsiToWide(error.category().name()) + L" " + std::to_wstring(error.value()) + L")";
	}
} // namespace BaseballSimulatorVR::Common::TextConversion

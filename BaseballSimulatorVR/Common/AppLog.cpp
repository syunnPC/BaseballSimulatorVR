#include "AppLog.hpp"

#include "TextConversion.hpp"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <mutex>
#include <string>

namespace BaseballSimulatorVR::Common::AppLog
{
	namespace
	{
		constexpr std::wstring_view LogFileStem = L"app";

		struct LoggerState final
		{
			std::mutex mutex{};
			std::filesystem::path directory{};
			Options options{};
			HANDLE file{ INVALID_HANDLE_VALUE };
			std::uintmax_t fileBytes{ 0U };
		};

		[[nodiscard]] LoggerState& GetState()
		{
			static LoggerState state{};
			return state;
		}

		[[nodiscard]] std::filesystem::path BuildLogFilePath(
		    const std::filesystem::path& directory, const int index)
		{
			std::wstring name{ LogFileStem };
			if (index > 0)
			{
				name += L"." + std::to_wstring(index);
			}
			name += L".log";
			return directory / name;
		}

		void CloseFile(LoggerState& state)
		{
			if (state.file != INVALID_HANDLE_VALUE)
			{
				CloseHandle(state.file);
				state.file = INVALID_HANDLE_VALUE;
			}
			state.fileBytes = 0U;
		}

		[[nodiscard]] bool OpenFile(LoggerState& state)
		{
			const std::filesystem::path path = BuildLogFilePath(state.directory, 0);
			state.file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
			    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL, nullptr);
			if (state.file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			LARGE_INTEGER size{};
			state.fileBytes = GetFileSizeEx(state.file, &size) && size.QuadPart > 0
			    ? static_cast<std::uintmax_t>(size.QuadPart)
			    : 0U;
			return true;
		}

		void RotateFiles(LoggerState& state)
		{
			CloseFile(state);
			const int retained = (std::max)(state.options.retainedRotatedFiles, 0);
			if (retained == 0)
			{
				DeleteFileW(BuildLogFilePath(state.directory, 0).c_str());
			}
			else
			{
				DeleteFileW(BuildLogFilePath(state.directory, retained).c_str());
				for (int index = retained - 1; index >= 0; --index)
				{
					MoveFileExW(BuildLogFilePath(state.directory, index).c_str(),
					    BuildLogFilePath(state.directory, index + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
				}
			}
			(void)OpenFile(state);
		}

		[[nodiscard]] std::wstring_view LevelName(const Level level) noexcept
		{
			switch (level)
			{
			case Level::Warning:
				return L"WARN";
			case Level::Error:
				return L"ERROR";
			case Level::Info:
			default:
				return L"INFO";
			}
		}

		[[nodiscard]] std::wstring BuildTimestamp()
		{
			SYSTEMTIME time{};
			GetSystemTime(&time);
			wchar_t buffer[32]{};
			(void)swprintf_s(buffer, std::size(buffer), L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			    static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
			    static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
			    static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
			    static_cast<unsigned>(time.wMilliseconds));
			return buffer;
		}
	} // namespace

	bool Initialize(const std::filesystem::path& directory, const Options& options)
	{
		LoggerState& state = GetState();
		const std::lock_guard lock(state.mutex);
		CloseFile(state);
		state.directory.clear();
		if (directory.empty())
		{
			return false;
		}
		std::error_code error{};
		std::filesystem::create_directories(directory, error);
		if (error)
		{
			const std::wstring message = L"[Log] cannot create log directory " + directory.wstring() +
			    L": " + TextConversion::DescribeErrorCode(error) + L"\n";
			OutputDebugStringW(message.c_str());
			return false;
		}
		state.directory = directory;
		state.options = options;
		if (!OpenFile(state))
		{
			state.directory.clear();
			return false;
		}
		return true;
	}

	void Shutdown()
	{
		LoggerState& state = GetState();
		const std::lock_guard lock(state.mutex);
		CloseFile(state);
		state.directory.clear();
	}

	std::filesystem::path GetLogFilePath()
	{
		LoggerState& state = GetState();
		const std::lock_guard lock(state.mutex);
		return state.directory.empty() ? std::filesystem::path{} : BuildLogFilePath(state.directory, 0);
	}

	void Write(const Level level, const std::wstring_view category, const std::wstring_view message)
	{
		std::wstring line = BuildTimestamp();
		line += L" [";
		line += LevelName(level);
		line += L"] [";
		line += category;
		line += L"] ";
		line += message;
		while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
		{
			line.pop_back();
		}
		line += L"\n";
		OutputDebugStringW(line.c_str());

		LoggerState& state = GetState();
		const std::lock_guard lock(state.mutex);
		if (state.file == INVALID_HANDLE_VALUE)
		{
			return;
		}
		line.insert(line.size() - 1U, 1U, L'\r');
		const std::string bytes = TextConversion::TryWideToUtf8(line).value_or(
		    TextConversion::TryWideToUtf8(L"<log message is not valid UTF-16>\r\n").value_or(std::string{}));
		if (state.fileBytes > 0U && state.fileBytes + bytes.size() > state.options.maximumFileBytes)
		{
			RotateFiles(state);
			if (state.file == INVALID_HANDLE_VALUE)
			{
				return;
			}
		}
		DWORD written = 0;
		if (WriteFile(state.file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
		{
			state.fileBytes += written;
		}
	}
} // namespace BaseballSimulatorVR::Common::AppLog

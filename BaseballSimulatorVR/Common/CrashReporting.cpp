#include "CrashReporting.hpp"

#include "AppLog.hpp"
#include "TextConversion.hpp"

#include <DbgHelp.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <mutex>

#pragma comment(lib, "dbghelp.lib")

namespace BaseballSimulatorVR::Common::CrashReporting
{
	namespace
	{
		constexpr std::wstring_view LogCategory = L"Crash";

		struct CrashState final
		{
			std::mutex mutex{};
			std::filesystem::path dumpDirectory{};
			bool handling{ false };
		};

		[[nodiscard]] CrashState& GetState()
		{
			static CrashState state{};
			return state;
		}

		void WriteCrashReport(EXCEPTION_POINTERS* const exception, const std::wstring_view reason)
		{
			CrashState& state = GetState();
			std::filesystem::path directory{};
			{
				const std::lock_guard lock(state.mutex);
				if (state.handling)
				{
					return;
				}
				state.handling = true;
				directory = state.dumpDirectory;
			}
			std::wstring error{};
			const std::optional<std::filesystem::path> dump = WriteMiniDump(exception, directory, error);
			std::wstring message{ reason };
			if (exception != nullptr && exception->ExceptionRecord != nullptr)
			{
				wchar_t code[32]{};
				(void)swprintf_s(code, std::size(code), L" (exception 0x%08lX)",
				    static_cast<unsigned long>(exception->ExceptionRecord->ExceptionCode));
				message += code;
			}
			message += dump.has_value() ? L"; minidump written to " + dump->wstring() : L"; minidump failed: " + error;
			AppLog::Error(LogCategory, message);
			AppLog::Shutdown();
		}

		LONG WINAPI UnhandledExceptionFilterCallback(EXCEPTION_POINTERS* const exception)
		{
			WriteCrashReport(exception, L"unhandled structured exception");
			return EXCEPTION_EXECUTE_HANDLER;
		}

		[[noreturn]] void TerminateHandler()
		{
			std::wstring reason = L"std::terminate";
			if (const std::exception_ptr current = std::current_exception())
			{
				try
				{
					std::rethrow_exception(current);
				}
				catch (const std::exception& error)
				{
					reason += L" after an uncaught exception: " + TextConversion::AnsiToWide(error.what());
				}
				catch (...)
				{
					reason += L" after an uncaught non-standard exception";
				}
			}
			WriteCrashReport(nullptr, reason);
			std::abort();
		}
	} // namespace

	std::filesystem::path GetLocalApplicationDataDirectory()
	{
		PWSTR knownFolderPath = nullptr;
		const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &knownFolderPath);
		std::filesystem::path directory{};
		if (SUCCEEDED(result) && knownFolderPath != nullptr)
		{
			directory = std::filesystem::path{ knownFolderPath } / L"BaseballSimulatorVR";
		}
		if (knownFolderPath != nullptr)
		{
			CoTaskMemFree(knownFolderPath);
		}
		return directory;
	}

	void Install(const std::filesystem::path& dumpDirectory)
	{
		{
			CrashState& state = GetState();
			const std::lock_guard lock(state.mutex);
			state.dumpDirectory = dumpDirectory;
		}
		SetUnhandledExceptionFilter(&UnhandledExceptionFilterCallback);
		std::set_terminate(&TerminateHandler);
	}

	std::optional<std::filesystem::path> WriteMiniDump(
	    EXCEPTION_POINTERS* const exception, const std::filesystem::path& directory, std::wstring& errorMessage)
	{
		errorMessage.clear();
		if (directory.empty())
		{
			errorMessage = L"no crash dump directory";
			return std::nullopt;
		}
		std::error_code directoryError{};
		std::filesystem::create_directories(directory, directoryError);
		if (directoryError)
		{
			errorMessage = TextConversion::DescribeErrorCode(directoryError);
			return std::nullopt;
		}
		SYSTEMTIME time{};
		GetLocalTime(&time);
		wchar_t name[64]{};
		(void)swprintf_s(name, std::size(name), L"crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
		    static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
		    static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
		    static_cast<unsigned long>(GetCurrentProcessId()));
		const std::filesystem::path path = directory / name;
		const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			errorMessage = TextConversion::FormatSystemErrorMessage(GetLastError());
			return std::nullopt;
		}
		MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
		exceptionInformation.ThreadId = GetCurrentThreadId();
		exceptionInformation.ExceptionPointers = exception;
		exceptionInformation.ClientPointers = FALSE;
		const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
		const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
		    exception != nullptr ? &exceptionInformation : nullptr, nullptr, nullptr);
		const DWORD writeError = GetLastError();
		CloseHandle(file);
		if (!written)
		{
			DeleteFileW(path.c_str());
			errorMessage = TextConversion::FormatSystemErrorMessage(writeError);
			return std::nullopt;
		}
		return path;
	}
} // namespace BaseballSimulatorVR::Common::CrashReporting

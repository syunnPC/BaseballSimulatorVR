#pragma once

#include "../Framework.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace BaseballSimulatorVR::Common::CrashReporting
{
	// %LOCALAPPDATA%\BaseballSimulatorVR (empty when the known folder cannot be resolved).
	[[nodiscard]] std::filesystem::path GetLocalApplicationDataDirectory();

	// Installs an unhandled-exception filter and a std::terminate handler that write a minidump
	// (MiniDumpWriteDump) into dumpDirectory and log the crash before the process ends.
	void Install(const std::filesystem::path& dumpDirectory);

	// Writes "crash-<local time>-<pid>.dmp" into directory. exception may be null (current state).
	[[nodiscard]] std::optional<std::filesystem::path> WriteMiniDump(
	    EXCEPTION_POINTERS* exception,
	    const std::filesystem::path& directory,
	    std::wstring& errorMessage);
} // namespace BaseballSimulatorVR::Common::CrashReporting

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace BaseballSimulatorVR::Common::AppLog
{
	enum class Level
	{
		Info,
		Warning,
		Error
	};

	struct Options final
	{
		// The active file is rotated to app.1.log ... once it would exceed this size.
		std::uintmax_t maximumFileBytes{ 1024U * 1024U };
		int retainedRotatedFiles{ 3 };
	};

	// Starts writing <directory>/app.log. Until then (and when the directory cannot be
	// created) messages only go to OutputDebugStringW. Safe to call from any thread.
	[[nodiscard]] bool Initialize(const std::filesystem::path& directory, const Options& options = {});
	void Shutdown();
	[[nodiscard]] std::filesystem::path GetLogFilePath();

	// Thread-safe; every message is also forwarded to OutputDebugStringW.
	void Write(Level level, std::wstring_view category, std::wstring_view message);

	inline void Info(const std::wstring_view category, const std::wstring_view message)
	{
		Write(Level::Info, category, message);
	}
	inline void Warning(const std::wstring_view category, const std::wstring_view message)
	{
		Write(Level::Warning, category, message);
	}
	inline void Error(const std::wstring_view category, const std::wstring_view message)
	{
		Write(Level::Error, category, message);
	}
} // namespace BaseballSimulatorVR::Common::AppLog

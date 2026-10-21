#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Tests
{
	inline std::size_t checkCount = 0;

	// Multiplier for GPU watchdogs and deadlines; TestMain reads it from
	// BASEBALLSIM_TEST_TIMEOUT_SCALE so slow CI machines and sanitizer builds
	// do not turn a long wait into a failure.
	inline double timeoutScale = 1.0;

	// Thrown by Skip(). Deliberately not a std::exception, so a suite that
	// catches std::exception cannot turn a skip into a pass by accident.
	struct SkipSuite final
	{
		std::string reason;
	};

	// Ends the current suite; TestMain reports it as SKIP instead of PASS.
	[[noreturn]] inline void Skip(std::string_view reason)
	{
		throw SkipSuite{ std::string(reason) };
	}

	[[nodiscard]] inline std::string FormatLocation(const std::source_location& location)
	{
		std::string_view file = location.file_name();
		if (const auto separator = file.find_last_of("/\\"); separator != std::string_view::npos)
			file.remove_prefix(separator + 1);
		return std::string(file) + ':' + std::to_string(location.line());
	}

	inline void Check(bool condition, std::string_view message,
	                  const std::source_location location = std::source_location::current())
	{
		++checkCount;
		if (!condition) throw std::runtime_error(FormatLocation(location) + ": " + std::string(message));
	}

	// True when a == b or |a - b| < max(absolute, relative * max(|a|, |b|)).
	// Pass relative = 0 for a purely absolute bound. NaN is never near anything.
	[[nodiscard]] inline bool Near(const double a, const double b, const double absolute = 1e-8,
	                               const double relative = 1e-9) noexcept
	{
		if (a == b) return true; // Also covers equal infinities.
		const double scale = (std::max)(std::abs(a), std::abs(b));
		return std::abs(a - b) < (std::max)(absolute, relative * scale);
	}

	// Scales a watchdog or deadline in milliseconds by timeoutScale, staying
	// below the Win32 INFINITE sentinel.
	[[nodiscard]] inline unsigned long ScaleTimeoutMilliseconds(const unsigned long milliseconds) noexcept
	{
		constexpr double Longest = 4294967294.0;
		const double scaled = std::ceil(static_cast<double>(milliseconds) * timeoutScale);
		return static_cast<unsigned long>((std::min)(scaled, Longest));
	}

	[[nodiscard]] inline std::chrono::milliseconds ScaleTimeout(const std::chrono::milliseconds timeout) noexcept
	{
		return std::chrono::milliseconds(ScaleTimeoutMilliseconds(static_cast<unsigned long>(timeout.count())));
	}

	inline std::string ReadFile(const std::filesystem::path& path,
	                            const std::source_location location = std::source_location::current())
	{
		std::ifstream input(path, std::ios::binary);
		Check(input.is_open(), "Test input file must be readable", location);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	class TempDirectory final
	{
	  public:
		TempDirectory()
		{
			static std::size_t sequence = 0;
			path = std::filesystem::temp_directory_path() /
			    ("BaseballSimulatorVR.Tests." +
			     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
			     "." + std::to_string(++sequence));
			Check(std::filesystem::create_directory(path), "Create isolated test directory");
		}
		TempDirectory(const TempDirectory&) = delete;
		TempDirectory& operator=(const TempDirectory&) = delete;
		~TempDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
		std::filesystem::path path;
	};
}

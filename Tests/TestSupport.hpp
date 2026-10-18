#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Tests
{
	inline std::size_t checkCount = 0;

	inline void Check(bool condition, std::string_view message)
	{
		++checkCount;
		if (!condition) throw std::runtime_error(std::string(message));
	}

	inline std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Check(input.is_open(), "Test input file must be readable");
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

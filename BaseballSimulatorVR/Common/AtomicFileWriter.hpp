#pragma once

#include "../Framework.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace BaseballSimulatorVR::Common
{
	struct FileOperationResult final
	{
		bool succeeded{ false };
		std::wstring errorMessage{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return succeeded;
		}
	};

	struct AtomicWriteOptions final
	{
		// Copy the file being replaced to "<name>.bak" before publishing the new content.
		bool keepBackup{ false };
		// When set, the replaced file only becomes the backup if its content passes this check,
		// so a corrupted file never overwrites the last good backup.
		std::function<bool(std::string_view existingContent)> isBackupContentValid{};
	};

	[[nodiscard]] std::filesystem::path BuildBackupFilePath(const std::filesystem::path& targetPath);

	// Reads a complete file as bytes; errorMessage describes failures.
	[[nodiscard]] std::optional<std::string> ReadWholeFile(
	    const std::filesystem::path& filePath, std::wstring& errorMessage);

	// Writes into a uniquely named sibling temporary file, flushes it to disk and publishes it with
	// MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH). A failed write never truncates the target, and
	// the temporary file is removed on every failure path.
	class AtomicFileWriter final
	{
	  public:
		explicit AtomicFileWriter(std::filesystem::path targetPath);
		AtomicFileWriter(const AtomicFileWriter&) = delete;
		AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;
		AtomicFileWriter(AtomicFileWriter&&) = delete;
		AtomicFileWriter& operator=(AtomicFileWriter&&) = delete;
		~AtomicFileWriter();

		[[nodiscard]] FileOperationResult Open();
		[[nodiscard]] bool Write(std::string_view bytes);
		[[nodiscard]] FileOperationResult Commit(const AtomicWriteOptions& options = {});
		void Discard() noexcept;

		[[nodiscard]] const std::filesystem::path& GetTemporaryPath() const noexcept
		{
			return m_temporaryPath;
		}

	  private:
		std::filesystem::path m_targetPath;
		std::filesystem::path m_temporaryPath{};
		HANDLE m_handle{ INVALID_HANDLE_VALUE };
		bool m_writeFailed{ false };
	};

	[[nodiscard]] FileOperationResult WriteFileAtomically(
	    const std::filesystem::path& targetPath,
	    std::string_view bytes,
	    const AtomicWriteOptions& options = {});
} // namespace BaseballSimulatorVR::Common

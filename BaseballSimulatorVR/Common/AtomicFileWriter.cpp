#include "AtomicFileWriter.hpp"

#include "TextConversion.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <utility>

namespace BaseballSimulatorVR::Common
{
	namespace
	{
		[[nodiscard]] std::wstring BuildUniqueSuffix()
		{
			static std::atomic<std::uint64_t> sequence{ 0U };
			FILETIME fileTime{};
			GetSystemTimeAsFileTime(&fileTime);
			ULARGE_INTEGER timestamp{};
			timestamp.LowPart = fileTime.dwLowDateTime;
			timestamp.HighPart = fileTime.dwHighDateTime;
			return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(timestamp.QuadPart) +
			       L"-" + std::to_wstring(sequence.fetch_add(1U, std::memory_order_relaxed));
		}

		[[nodiscard]] bool FileExists(const std::filesystem::path& path)
		{
			std::error_code error{};
			return std::filesystem::is_regular_file(path, error) && !error;
		}

		void BackUpReplacedFile(const std::filesystem::path& targetPath, const AtomicWriteOptions& options)
		{
			if (!options.keepBackup || !FileExists(targetPath))
			{
				return;
			}
			if (options.isBackupContentValid)
			{
				std::wstring readError{};
				const std::optional<std::string> existing = ReadWholeFile(targetPath, readError);
				if (!existing.has_value() || !options.isBackupContentValid(*existing))
				{
					return;
				}
			}
			// Best effort: a missing backup must not prevent saving the new settings.
			(void)CopyFileW(targetPath.c_str(), BuildBackupFilePath(targetPath).c_str(), FALSE);
		}
	} // namespace

	std::filesystem::path BuildBackupFilePath(const std::filesystem::path& targetPath)
	{
		return std::filesystem::path{ targetPath.wstring() + L".bak" };
	}

	std::optional<std::string> ReadWholeFile(const std::filesystem::path& filePath, std::wstring& errorMessage)
	{
		errorMessage.clear();
		const HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			errorMessage = L"Cannot open " + filePath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(GetLastError());
			return std::nullopt;
		}

		std::string content{};
		LARGE_INTEGER size{};
		if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
		    size.QuadPart < static_cast<LONGLONG>(256LL * 1024LL * 1024LL))
		{
			content.reserve(static_cast<std::size_t>(size.QuadPart));
		}
		char buffer[64 * 1024];
		for (;;)
		{
			DWORD read = 0;
			if (!ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr))
			{
				errorMessage = L"Cannot read " + filePath.wstring() + L": " +
				    TextConversion::FormatSystemErrorMessage(GetLastError());
				CloseHandle(file);
				return std::nullopt;
			}
			if (read == 0)
			{
				break;
			}
			content.append(buffer, read);
		}
		CloseHandle(file);
		return content;
	}

	AtomicFileWriter::AtomicFileWriter(std::filesystem::path targetPath) : m_targetPath(std::move(targetPath))
	{
	}

	AtomicFileWriter::~AtomicFileWriter()
	{
		Discard();
	}

	FileOperationResult AtomicFileWriter::Open()
	{
		Discard();
		if (m_targetPath.empty() || m_targetPath.filename().empty())
		{
			return { false, L"Output file path is empty." };
		}
		m_temporaryPath = std::filesystem::path{ m_targetPath.wstring() + L"." + BuildUniqueSuffix() + L".tmp" };
		m_handle = CreateFileW(m_temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
		    FILE_ATTRIBUTE_NORMAL, nullptr);
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			const DWORD error = GetLastError();
			m_temporaryPath.clear();
			return { false, L"Failed to create a temporary file next to " + m_targetPath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(error) };
		}
		m_writeFailed = false;
		return { true, {} };
	}

	bool AtomicFileWriter::Write(std::string_view bytes)
	{
		if (m_handle == INVALID_HANDLE_VALUE || m_writeFailed)
		{
			return false;
		}
		while (!bytes.empty())
		{
			const DWORD requested = static_cast<DWORD>((std::min)(
			    bytes.size(), static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
			DWORD written = 0;
			if (!WriteFile(m_handle, bytes.data(), requested, &written, nullptr) || written == 0)
			{
				m_writeFailed = true;
				return false;
			}
			bytes.remove_prefix(written);
		}
		return true;
	}

	FileOperationResult AtomicFileWriter::Commit(const AtomicWriteOptions& options)
	{
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			return { false, L"The temporary output file is not open." };
		}
		if (m_writeFailed)
		{
			Discard();
			return { false, L"Failed while writing " + m_targetPath.wstring() + L"." };
		}
		if (!FlushFileBuffers(m_handle))
		{
			const DWORD error = GetLastError();
			Discard();
			return { false, L"Failed to flush " + m_targetPath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(error) };
		}
		const BOOL closed = CloseHandle(m_handle);
		m_handle = INVALID_HANDLE_VALUE;
		if (!closed)
		{
			const DWORD error = GetLastError();
			Discard();
			return { false, L"Failed to close the temporary file for " + m_targetPath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(error) };
		}
		BackUpReplacedFile(m_targetPath, options);
		if (!MoveFileExW(m_temporaryPath.c_str(), m_targetPath.c_str(),
		        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			const DWORD error = GetLastError();
			Discard();
			return { false, L"Failed to replace " + m_targetPath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(error) };
		}
		m_temporaryPath.clear();
		return { true, {} };
	}

	void AtomicFileWriter::Discard() noexcept
	{
		if (m_handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_handle);
			m_handle = INVALID_HANDLE_VALUE;
		}
		if (!m_temporaryPath.empty())
		{
			DeleteFileW(m_temporaryPath.c_str());
			m_temporaryPath.clear();
		}
	}

	FileOperationResult WriteFileAtomically(
	    const std::filesystem::path& targetPath, const std::string_view bytes, const AtomicWriteOptions& options)
	{
		AtomicFileWriter writer{ targetPath };
		if (FileOperationResult opened = writer.Open(); !opened)
		{
			return opened;
		}
		if (!writer.Write(bytes))
		{
			const DWORD error = GetLastError();
			writer.Discard();
			return { false, L"Failed while writing " + targetPath.wstring() + L": " +
			    TextConversion::FormatSystemErrorMessage(error) };
		}
		return writer.Commit(options);
	}
} // namespace BaseballSimulatorVR::Common

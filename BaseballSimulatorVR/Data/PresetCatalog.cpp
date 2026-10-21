#include "PresetCatalog.hpp"

#include "../Common/PitchPresetUi.hpp"
#include "../Common/TextConversion.hpp"
#include "../Framework.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace BaseballSimulatorVR::Data
{
	namespace
	{
		constexpr std::wstring_view JsonExtension = L".json";
		constexpr std::wstring_view RightHandSuffix = L".RHP";
		constexpr std::wstring_view LeftHandSuffix = L".LHP";
		constexpr std::wstring_view DefaultSuffix = L".default.json";
		constexpr std::wstring_view BrokenMarker = L".broken-";

		[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error{};
			return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
		}

		[[nodiscard]] std::filesystem::path Normalize(const std::filesystem::path& path)
		{
			if (path.empty())
			{
				return {};
			}
			std::error_code error{};
			const std::filesystem::path absolute = path.is_absolute() ? path : std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		[[nodiscard]] std::wstring StripHandSuffix(std::wstring stem)
		{
			for (const std::wstring_view suffix : { RightHandSuffix, LeftHandSuffix })
			{
				if (stem.size() > suffix.size() && std::wstring_view{ stem }.ends_with(suffix))
				{
					stem.resize(stem.size() - suffix.size());
					break;
				}
			}
			return stem;
		}

		[[nodiscard]] std::wstring BuildTimestampDirectoryName()
		{
			SYSTEMTIME time{};
			GetLocalTime(&time);
			wchar_t buffer[32]{};
			(void)swprintf_s(buffer, std::size(buffer), L"%04u%02u%02u-%02u%02u%02u",
			    static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
			    static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute),
			    static_cast<unsigned>(time.wSecond));
			return buffer;
		}

		[[nodiscard]] bool CopyIfMissing(const std::filesystem::path& source, const std::filesystem::path& destination,
		    std::wstring& errorMessage)
		{
			if (IsRegularFile(destination))
			{
				return true;
			}
			if (!IsRegularFile(source))
			{
				errorMessage = L"Bundled default settings file is missing: " + source.wstring();
				return false;
			}
			std::error_code error{};
			std::filesystem::copy_file(source, destination, std::filesystem::copy_options::skip_existing, error);
			if (error)
			{
				errorMessage = L"Failed to create " + destination.wstring() + L" from " + source.wstring() + L": " +
				    Common::TextConversion::DescribeErrorCode(error);
				return false;
			}
			return true;
		}
	} // namespace

	bool IsPitchSettingsFileName(const std::wstring_view fileName) noexcept
	{
		return fileName.size() > PresetLayout::PitchSettingsPrefix.size() + JsonExtension.size() &&
		       fileName.starts_with(PresetLayout::PitchSettingsPrefix) && fileName.ends_with(JsonExtension) &&
		       fileName.find(BrokenMarker) == std::wstring_view::npos;
	}

	std::wstring PitchSettingsEntry::BuildLabel() const
	{
		return Common::BuildPitchPresetLabel(displayName, pitcherThrowsLeftHanded);
	}

	PresetCatalog::PresetCatalog(std::filesystem::path bundledPresetsDirectory, std::filesystem::path userDirectory)
	    : m_bundledDirectory(Normalize(bundledPresetsDirectory)), m_userDirectory(Normalize(userDirectory))
	{
	}

	bool PresetCatalog::IsBundledPresetsDirectory(const std::filesystem::path& directory)
	{
		return !directory.empty() &&
		       IsRegularFile(directory / PresetLayout::PitchDirectoryName / PresetLayout::DefaultPitchSettingsFileName) &&
		       IsRegularFile(directory / PresetLayout::EnvironmentDirectoryName /
		           PresetLayout::DefaultEnvironmentSettingsFileName) &&
		       IsRegularFile(directory / PresetLayout::PlayDirectoryName / PresetLayout::DefaultPlaySettingsFileName);
	}

	std::filesystem::path PresetCatalog::GetBundledPitchDirectory() const
	{
		return m_bundledDirectory.empty() ? std::filesystem::path{} : m_bundledDirectory / PresetLayout::PitchDirectoryName;
	}

	std::filesystem::path PresetCatalog::GetBundledEnvironmentDirectory() const
	{
		return m_bundledDirectory.empty() ? std::filesystem::path{}
		                                  : m_bundledDirectory / PresetLayout::EnvironmentDirectoryName;
	}

	std::filesystem::path PresetCatalog::GetBundledPlayDirectory() const
	{
		return m_bundledDirectory.empty() ? std::filesystem::path{} : m_bundledDirectory / PresetLayout::PlayDirectoryName;
	}

	void PresetCatalog::AppendEntriesForFile(const std::filesystem::path& file, const PresetOrigin origin,
	    std::vector<PitchSettingsEntry>& entries, std::vector<std::wstring>* const warnings) const
	{
		std::wstring error{};
		const std::optional<PitchRuntimeSettings> settings = m_storage.LoadPitchSettings(file.wstring(), error);
		if (!settings.has_value())
		{
			if (warnings != nullptr)
			{
				warnings->push_back(file.wstring() + L": " + error);
			}
			return;
		}
		const std::wstring fileName = file.filename().wstring();
		std::wstring displayName = settings->displayName;
		if (displayName.empty())
		{
			displayName = fileName == PresetLayout::UserPitchSettingsFileName
			    ? std::wstring{ PresetLayout::UserPitchSettingsDisplayName }
			    : fileName;
		}
		if (!settings->mirrorForLeftHandedPitcher)
		{
			entries.push_back(PitchSettingsEntry{ fileName, file, origin, false, displayName, std::nullopt });
			return;
		}
		const std::wstring stem = StripHandSuffix(file.stem().wstring());
		const bool baseLeft = settings->pitcherThrowsLeftHanded;
		const auto idFor = [&](const bool leftHanded)
		{ return stem + std::wstring{ leftHanded ? LeftHandSuffix : RightHandSuffix } + std::wstring{ JsonExtension }; };
		entries.push_back(PitchSettingsEntry{ idFor(baseLeft), file, origin, false, displayName, baseLeft });
		entries.push_back(PitchSettingsEntry{ idFor(!baseLeft), file, origin, true, displayName, !baseLeft });
	}

	std::vector<PitchSettingsEntry> PresetCatalog::EnumeratePitchSettings(std::vector<std::wstring>* const warnings) const
	{
		std::vector<PitchSettingsEntry> bundled{};
		const std::filesystem::path pitchDirectory = GetBundledPitchDirectory();
		std::error_code error{};
		if (!pitchDirectory.empty() && std::filesystem::is_directory(pitchDirectory, error))
		{
			std::vector<std::filesystem::path> files{};
			for (std::filesystem::recursive_directory_iterator iterator(
			         pitchDirectory, std::filesystem::directory_options::skip_permission_denied, error);
			     !error && iterator != std::filesystem::recursive_directory_iterator{}; iterator.increment(error))
			{
				std::error_code entryError{};
				const std::wstring name = iterator->path().filename().wstring();
				if (iterator->is_regular_file(entryError) && IsPitchSettingsFileName(name) && !name.ends_with(DefaultSuffix))
				{
					files.push_back(iterator->path());
				}
			}
			if (error && warnings != nullptr)
			{
				warnings->push_back(L"Bundled presets could not be listed: " + Common::TextConversion::DescribeErrorCode(error));
			}
			for (const std::filesystem::path& file : files)
			{
				AppendEntriesForFile(file, PresetOrigin::Bundled, bundled, warnings);
			}
		}

		std::vector<PitchSettingsEntry> user{};
		error.clear();
		if (!m_userDirectory.empty() && std::filesystem::is_directory(m_userDirectory, error))
		{
			std::vector<std::filesystem::path> files{};
			for (std::filesystem::directory_iterator iterator(
			         m_userDirectory, std::filesystem::directory_options::skip_permission_denied, error);
			     !error && iterator != std::filesystem::directory_iterator{}; iterator.increment(error))
			{
				std::error_code entryError{};
				if (iterator->is_regular_file(entryError) && IsPitchSettingsFileName(iterator->path().filename().wstring()))
				{
					files.push_back(iterator->path());
				}
			}
			if (error && warnings != nullptr)
			{
				warnings->push_back(L"User pitch settings could not be listed: " +
				    Common::TextConversion::DescribeErrorCode(error));
			}
			for (const std::filesystem::path& file : files)
			{
				AppendEntriesForFile(file, PresetOrigin::User, user, warnings);
			}
		}

		std::vector<PitchSettingsEntry> merged = std::move(user);
		std::set<std::wstring> userIds{};
		for (const PitchSettingsEntry& entry : merged)
		{
			userIds.insert(entry.id);
		}
		for (PitchSettingsEntry& entry : bundled)
		{
			if (!userIds.contains(entry.id))
			{
				merged.push_back(std::move(entry));
			}
		}
		std::sort(merged.begin(), merged.end(),
		    [](const PitchSettingsEntry& left, const PitchSettingsEntry& right) { return left.id < right.id; });
		merged.erase(std::unique(merged.begin(), merged.end(),
		                 [](const PitchSettingsEntry& left, const PitchSettingsEntry& right) { return left.id == right.id; }),
		    merged.end());
		return merged;
	}

	std::optional<PitchSettingsEntry> PresetCatalog::FindPitchSettings(const std::wstring_view id) const
	{
		for (PitchSettingsEntry& entry : EnumeratePitchSettings())
		{
			if (entry.id == id)
			{
				return std::move(entry);
			}
		}
		return std::nullopt;
	}

	PitchSettingsEntry PresetCatalog::DescribeFile(const std::filesystem::path& filePath) const
	{
		const std::filesystem::path normalized = Normalize(filePath);
		std::vector<PitchSettingsEntry> entries{};
		AppendEntriesForFile(normalized, IsBundledPath(normalized) ? PresetOrigin::Bundled : PresetOrigin::User, entries,
		    nullptr);
		if (!entries.empty())
		{
			// An explicit file always means its own (non-mirrored) content.
			return entries.front();
		}
		return PitchSettingsEntry{ normalized.filename().wstring(), normalized,
			IsBundledPath(normalized) ? PresetOrigin::Bundled : PresetOrigin::User, false,
			normalized.filename().wstring(), std::nullopt };
	}

	std::optional<PitchRuntimeSettings> PresetCatalog::LoadPitchSettings(
	    const PitchSettingsEntry& entry, std::wstring& errorMessage) const
	{
		std::optional<PitchRuntimeSettings> settings = m_storage.LoadPitchSettings(entry.sourcePath.wstring(), errorMessage);
		if (!settings.has_value() || !entry.mirrored)
		{
			return settings;
		}
		return MirrorPitchSettingsForOppositeHand(*settings);
	}

	std::filesystem::path PresetCatalog::ResolveEnvironmentReference(
	    const std::wstring_view reference, const std::filesystem::path& owningFile) const
	{
		const std::filesystem::path referencePath{ reference.empty() ? DefaultEnvironmentSettingsReference : reference };
		if (referencePath.is_absolute())
		{
			return referencePath.lexically_normal();
		}
		std::vector<std::filesystem::path> candidates{};
		const std::filesystem::path owner = Normalize(owningFile);
		if (owner.has_parent_path())
		{
			candidates.push_back((owner.parent_path() / referencePath).lexically_normal());
		}
		if (!m_userDirectory.empty())
		{
			candidates.push_back((m_userDirectory / referencePath).lexically_normal());
		}
		if (!m_bundledDirectory.empty())
		{
			candidates.push_back((GetBundledEnvironmentDirectory() / referencePath).lexically_normal());
			if (referencePath.filename() == PresetLayout::UserEnvironmentSettingsFileName)
			{
				candidates.push_back(GetBundledEnvironmentDirectory() / PresetLayout::DefaultEnvironmentSettingsFileName);
			}
		}
		for (const std::filesystem::path& candidate : candidates)
		{
			if (IsRegularFile(candidate))
			{
				return candidate;
			}
		}
		return candidates.empty() ? referencePath : candidates.front();
	}

	std::filesystem::path PresetCatalog::ResolveEnvironmentSaveTarget(const std::filesystem::path& environmentFile) const
	{
		const std::filesystem::path normalized = Normalize(environmentFile);
		if (!IsBundledPath(normalized) || m_userDirectory.empty())
		{
			return normalized;
		}
		if (normalized.filename() == PresetLayout::DefaultEnvironmentSettingsFileName)
		{
			return m_userDirectory / PresetLayout::UserEnvironmentSettingsFileName;
		}
		return m_userDirectory / normalized.filename();
	}

	std::filesystem::path PresetCatalog::ResolvePitchSaveTarget(const PitchSettingsEntry& entry) const
	{
		if (entry.origin == PresetOrigin::User && !entry.mirrored && !IsBundledPath(entry.sourcePath))
		{
			return entry.sourcePath;
		}
		return m_userDirectory.empty() ? std::filesystem::path{ entry.id } : m_userDirectory / entry.id;
	}

	bool PresetCatalog::IsBundledPath(const std::filesystem::path& path) const
	{
		if (m_bundledDirectory.empty() || path.empty())
		{
			return false;
		}
		const std::filesystem::path relative = Normalize(path).lexically_relative(m_bundledDirectory);
		return !relative.empty() && *relative.begin() != L"..";
	}

	std::filesystem::path PresetCatalog::GetUserPlaySettingsPath() const
	{
		return m_userDirectory / PresetLayout::UserPlaySettingsFileName;
	}

	std::filesystem::path PresetCatalog::GetUserEnvironmentSettingsPath() const
	{
		return m_userDirectory / PresetLayout::UserEnvironmentSettingsFileName;
	}

	std::filesystem::path PresetCatalog::GetUserPitchSettingsPath() const
	{
		return m_userDirectory / PresetLayout::UserPitchSettingsFileName;
	}

	std::filesystem::path PresetCatalog::FindBundledDefaultFor(const std::filesystem::path& userFile) const
	{
		if (m_bundledDirectory.empty())
		{
			return {};
		}
		const std::filesystem::path name = userFile.filename();
		std::filesystem::path candidate{};
		if (name == PresetLayout::UserPlaySettingsFileName)
		{
			candidate = GetBundledPlayDirectory() / PresetLayout::DefaultPlaySettingsFileName;
		}
		else if (name == PresetLayout::UserEnvironmentSettingsFileName)
		{
			candidate = GetBundledEnvironmentDirectory() / PresetLayout::DefaultEnvironmentSettingsFileName;
		}
		else if (name == PresetLayout::UserPitchSettingsFileName)
		{
			candidate = GetBundledPitchDirectory() / PresetLayout::DefaultPitchSettingsFileName;
		}
		return IsRegularFile(candidate) ? candidate : std::filesystem::path{};
	}

	bool PresetCatalog::EnsureUserFiles(std::wstring& errorMessage) const
	{
		errorMessage.clear();
		if (m_userDirectory.empty())
		{
			errorMessage = L"The user settings directory is not set.";
			return false;
		}
		std::error_code error{};
		std::filesystem::create_directories(m_userDirectory, error);
		if (error)
		{
			errorMessage = L"Failed to create the user settings directory " + m_userDirectory.wstring() + L": " +
			    Common::TextConversion::DescribeErrorCode(error);
			return false;
		}
		for (const std::filesystem::path& userFile :
		     { GetUserPlaySettingsPath(), GetUserEnvironmentSettingsPath(), GetUserPitchSettingsPath() })
		{
			if (IsRegularFile(userFile))
			{
				continue;
			}
			const std::filesystem::path source = FindBundledDefaultFor(userFile);
			if (source.empty())
			{
				errorMessage = L"Cannot create " + userFile.wstring() +
				    L": its bundled default in the Presets folder is missing.";
				return false;
			}
			if (!CopyIfMissing(source, userFile, errorMessage))
			{
				return false;
			}
		}
		return true;
	}

	std::vector<std::filesystem::path> PresetCatalog::FindLegacyProvisionedCopies() const
	{
		std::vector<std::filesystem::path> copies{};
		if (m_userDirectory.empty() || m_bundledDirectory.empty())
		{
			return copies;
		}
		std::set<std::wstring> bundledNames{};
		const PresetCatalog bundledOnly{ m_bundledDirectory, {} };
		for (const PitchSettingsEntry& entry : bundledOnly.EnumeratePitchSettings())
		{
			bundledNames.insert(entry.id);
			bundledNames.insert(entry.sourcePath.filename().wstring());
		}
		std::error_code error{};
		for (std::filesystem::directory_iterator iterator(GetBundledEnvironmentDirectory(),
		         std::filesystem::directory_options::skip_permission_denied, error);
		     !error && iterator != std::filesystem::directory_iterator{}; iterator.increment(error))
		{
			const std::wstring name = iterator->path().filename().wstring();
			if (name.ends_with(JsonExtension) && !name.ends_with(DefaultSuffix))
			{
				bundledNames.insert(name);
			}
		}
		for (const std::wstring& name : bundledNames)
		{
			const std::filesystem::path candidate = m_userDirectory / name;
			if (!name.ends_with(L".user.json") && IsRegularFile(candidate))
			{
				copies.push_back(candidate);
			}
		}
		return copies;
	}

	std::optional<std::filesystem::path> PresetCatalog::MoveToLegacyBackup(
	    const std::vector<std::filesystem::path>& files, std::wstring& errorMessage) const
	{
		errorMessage.clear();
		if (files.empty())
		{
			return std::nullopt;
		}
		std::filesystem::path backupDirectory =
		    m_userDirectory / PresetLayout::LegacyPresetBackupDirectoryName / BuildTimestampDirectoryName();
		std::error_code error{};
		for (int attempt = 1; std::filesystem::exists(backupDirectory, error) && attempt < 100; ++attempt)
		{
			backupDirectory = m_userDirectory / PresetLayout::LegacyPresetBackupDirectoryName /
			    (BuildTimestampDirectoryName() + L"-" + std::to_wstring(attempt));
		}
		std::filesystem::create_directories(backupDirectory, error);
		if (error)
		{
			errorMessage = L"Failed to create " + backupDirectory.wstring() + L": " +
			    Common::TextConversion::DescribeErrorCode(error);
			return std::nullopt;
		}
		for (const std::filesystem::path& file : files)
		{
			const std::filesystem::path destination = backupDirectory / file.filename();
			if (!MoveFileExW(file.c_str(), destination.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
			{
				errorMessage = L"Failed to move " + file.wstring() + L" to " + destination.wstring() + L": " +
				    Common::TextConversion::FormatSystemErrorMessage(GetLastError());
				return backupDirectory;
			}
		}
		return backupDirectory;
	}
} // namespace BaseballSimulatorVR::Data

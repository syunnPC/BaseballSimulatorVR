#pragma once

#include "../Baseball/PitchParameters.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;

namespace BaseballSimulatorVR::Data
{
	struct OperationResult final
	{
		bool succeeded{ false };
		std::wstring errorMessage{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return succeeded;
		}
	};

	struct PitchEventLogResult final
	{
		std::optional<std::int64_t> eventId{};
		std::wstring errorMessage{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return eventId.has_value();
		}
	};

	class SessionLogRepository final
	{
	  public:
		SessionLogRepository() = default;
		SessionLogRepository(const SessionLogRepository&) = delete;
		SessionLogRepository& operator=(const SessionLogRepository&) = delete;
		SessionLogRepository(SessionLogRepository&&) = delete;
		SessionLogRepository& operator=(SessionLogRepository&&) = delete;

		~SessionLogRepository();

		[[nodiscard]] OperationResult Initialize(std::wstring_view databasePath);
		[[nodiscard]] bool Initialize(std::wstring_view databasePath, std::wstring& errorMessage);
		void Shutdown();
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] OperationResult ExportCurrentSessionCsv(
		    const std::filesystem::path& filePath) const;

		[[nodiscard]] PitchEventLogResult LogPitchEvent(
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note);
		[[nodiscard]] std::optional<std::int64_t> LogPitchEvent(
		    std::wstring_view eventType,
		    std::wstring_view presetName,
		    std::wstring_view integratorName,
		    const Baseball::PitchParameters& pitchParameters,
		    std::wstring_view note,
		    std::wstring& errorMessage);
		[[nodiscard]] OperationResult UpsertPitchEventResult(
		    std::int64_t eventId,
		    std::wstring_view resultLabel,
		    double resultScore,
		    std::wstring_view note);
		[[nodiscard]] bool UpsertPitchEventResult(
		    std::int64_t eventId,
		    std::wstring_view resultLabel,
		    double resultScore,
		    std::wstring_view note,
		    std::wstring& errorMessage);

	  private:
		[[nodiscard]] bool EnsureSchema(std::wstring& errorMessage);
		[[nodiscard]] bool EnsureColumnExists(
		    std::string_view tableName,
		    std::string_view columnName,
		    std::string_view columnDefinition,
		    std::wstring& errorMessage);

		static std::string ToUtf8(std::wstring_view value);
		static std::wstring ToWide(std::string_view value);

		sqlite3* m_database{ nullptr };
		std::wstring m_sessionId{};
	};
} // namespace BaseballSimulatorVR::Data

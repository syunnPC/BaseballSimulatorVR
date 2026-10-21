#include "Framework.hpp"

#include "Common/AppLog.hpp"
#include "Common/CrashReporting.hpp"
#include "Common/TextConversion.hpp"
#include "Core/Application.hpp"
#include "Data/PresetRepository.hpp"
#include "Render/Dx12RenderBackend.hpp"
#include "XR/OpenXrSystem.hpp"

#include <exception>
#include <memory>
#include <string>

namespace
{
	int ReportFatalException(const std::wstring& description)
	{
		BaseballSimulatorVR::Common::AppLog::Error(L"Crash", description);
		const std::filesystem::path logPath = BaseballSimulatorVR::Common::AppLog::GetLogFilePath();
		std::wstring message = L"BaseballSimulatorVR stopped because of an unexpected error:\n" + description;
		if (!logPath.empty())
		{
			message += L"\n\nDetails were written to:\n" + logPath.wstring();
		}
		MessageBoxW(nullptr, message.c_str(), L"BaseballSimulatorVR error", MB_OK | MB_ICONERROR);
		return -100;
	}
} // namespace

int WINAPI wWinMain(HINSTANCE instanceHandle, [[maybe_unused]] HINSTANCE previousInstance, [[maybe_unused]] PWSTR commandLine, int commandShow)
{
	namespace Common = BaseballSimulatorVR::Common;
	// %LOCALAPPDATA%\BaseballSimulatorVR\logs\app.log (rotated) and ...\crashdumps\*.dmp
	const std::filesystem::path applicationDataDirectory = Common::CrashReporting::GetLocalApplicationDataDirectory();
	if (!applicationDataDirectory.empty())
	{
		(void)Common::AppLog::Initialize(applicationDataDirectory / L"logs");
		Common::CrashReporting::Install(applicationDataDirectory / L"crashdumps");
	}
	Common::AppLog::Info(L"Startup", L"BaseballSimulatorVR starting");

	int exitCode = -100;
	try
	{
		BaseballSimulatorVR::Core::Application application(
		    std::make_unique<BaseballSimulatorVR::Xr::OpenXrSystem>(),
		    std::make_unique<BaseballSimulatorVR::Render::Dx12RenderBackend>(),
		    BaseballSimulatorVR::Data::PresetRepository{});

		exitCode = application.Run(instanceHandle, commandShow);
	}
	catch (const std::exception& error)
	{
		exitCode = ReportFatalException(L"Unhandled exception: " + Common::TextConversion::AnsiToWide(error.what()));
	}
	catch (...)
	{
		exitCode = ReportFatalException(L"Unhandled non-standard exception.");
	}

	Common::AppLog::Info(L"Startup", L"BaseballSimulatorVR exiting with code " + std::to_wstring(exitCode));
	Common::AppLog::Shutdown();
	return exitCode;
}

#include "Framework.hpp"

#include "Core/Application.hpp"
#include "Data/PresetRepository.hpp"
#include "Render/Dx12RenderBackend.hpp"
#include "XR/NullXrSystem.hpp"

#include <memory>

int WINAPI wWinMain(HINSTANCE instanceHandle, [[maybe_unused]] HINSTANCE previousInstance, [[maybe_unused]] PWSTR commandLine, int commandShow)
{
	BaseballSimulatorVR::Core::Application application(
	    std::make_unique<BaseballSimulatorVR::Xr::NullXrSystem>(),
	    std::make_unique<BaseballSimulatorVR::Render::Dx12RenderBackend>(),
	    BaseballSimulatorVR::Data::PresetRepository{});

	return application.Run(instanceHandle, commandShow);
}

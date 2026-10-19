#include "TestSupport.hpp"
#include "GpuQueueGate.hpp"
#include "../BaseballSimulatorVR/Render/Dx12RenderBackend.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <vector>

#if defined(_WIN64)
#include <d3d12sdklayers.h>

namespace
{
	class HiddenRenderWindow final
	{
	  public:
		HiddenRenderWindow()
		{
			WNDCLASSW windowClass{};
			windowClass.lpfnWndProc = DefWindowProcW;
			windowClass.hInstance = GetModuleHandleW(nullptr);
			windowClass.lpszClassName = ClassName;
			Tests::Check(RegisterClassW(&windowClass) != 0, "Register hidden render test window class");
			handle = CreateWindowExW(0, ClassName, L"Desktop resize integration test",
			    WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, windowClass.hInstance, nullptr);
			if (handle == nullptr)
			{
				UnregisterClassW(ClassName, windowClass.hInstance);
				Tests::Check(false, "Create hidden render test window");
			}
		}
		~HiddenRenderWindow()
		{
			if (handle != nullptr) DestroyWindow(handle);
			UnregisterClassW(ClassName, GetModuleHandleW(nullptr));
		}
		void ResizeClient(LONG width, LONG height) const
		{
			RECT rectangle{ 0, 0, width, height };
			Tests::Check(AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0) != FALSE,
			    "Compute outer extent for an exact client size");
			Tests::Check(SetWindowPos(handle, nullptr, 0, 0,
			    rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
			    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
			    "Resize the hidden desktop client");
		}
		HWND handle{ nullptr };
	  private:
		static constexpr const wchar_t* ClassName = L"BaseballSimulatorVR.DesktopResizeTests";
	};

	[[nodiscard]] bool HasD3d12Runtime()
	{
		Microsoft::WRL::ComPtr<IDXGIFactory7> factory{};
		const HRESULT factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
		if (FAILED(factoryResult))
		{
			std::cout << "SKIP desktop resize: DXGI 1.6 factory unavailable (HRESULT "
			    << std::hex << factoryResult << std::dec << ")\n";
			return false;
		}
		for (UINT index = 0;; ++index)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter{};
			const HRESULT adapterResult = factory->EnumAdapters1(index, &adapter);
			if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
			Tests::Check(SUCCEEDED(adapterResult), "Enumerate DXGI adapters without an unexpected runtime error");
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
			    __uuidof(ID3D12Device), nullptr))) return true;
		}
		Microsoft::WRL::ComPtr<IDXGIAdapter> warp{};
		if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
		    SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
		        __uuidof(ID3D12Device), nullptr))) return true;
		std::cout << "SKIP desktop resize: neither hardware nor WARP supports D3D12 feature level 11.0\n";
		return false;
	}
}
#endif

void RunDesktopRenderResizeTests()
{
#if defined(_WIN64)
	if (!HasD3d12Runtime()) return;
	// When installed, the debug layer also verifies resource lifetime/state during
	// ResizeBuffers. Its absence does not skip the real GPU/WARP integration.
	Microsoft::WRL::ComPtr<ID3D12Debug> debug{};
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
	HiddenRenderWindow window;
	Tests::Check(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window.handle),
	    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,
	    "Desktop render window uses per-monitor V2 physical-pixel DPI coordinates");
	window.ResizeClient(640, 360);
	BaseballSimulatorVR::Render::Dx12RenderBackend backend;
	Tests::Check(backend.Initialize(window.handle), "Initialize the real DX12 backend after runtime support is confirmed");
	BaseballSimulatorVR::Common::ViewProjectionState mirrorView{};
	mirrorView.isValid = true;
	mirrorView.position = { 0.0, 1.65, 0.0 };
	backend.SetDesktopMirrorView(mirrorView);
	BaseballSimulatorVR::Common::FieldPresentationState field{};
	field.statistics.visible = true;
	field.statistics.pitchName = L"山本由伸・フォーシーム";
	field.statistics.pitchSpeedMetersPerSecond = 43.1;
	field.statistics.pitchSpinRpm = 2450.0;
	field.statistics.hasBattedMetrics = true;
	field.statistics.battedLaunchSpeedMetersPerSecond = 43.315628;
	field.statistics.battedLaunchAngleDegrees = 27.5;
	field.statistics.battedDistanceMeters = 114.25;
	field.statistics.battedMaxHeightMeters = 25.9;
	field.pitcher.visible = true;
	field.pitcher.secondsFromRelease = -0.5;
	field.pitcher.releasePositionWorld = { 0.0, 1.98, -19.94 };
	backend.SetFieldPresentationState(field);
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> informationQueue{};
	(void)backend.GetGraphicsBinding().device->QueryInterface(IID_PPV_ARGS(&informationQueue));
	Tests::Check(!debug || informationQueue, "An enabled D3D12 debug layer exposes its message queue");
	std::wcout << L"Desktop resize integration: " << backend.GetBackendName()
	    << (debug ? L", D3D12 debug layer enabled\n" : L", D3D12 debug layer unavailable\n");
	const auto renderFrame = [&]
	{
		const auto before = backend.GetDesktopRenderStatus().presentedFrameCount;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		do
		{
			backend.BeginFrame();
			backend.RenderSimulation({});
			backend.EndFrame();
			const auto status = backend.GetDesktopRenderStatus();
			if (status.failed || status.suspended || status.presentedFrameCount > before) break;
			Sleep(1);
		} while (std::chrono::steady_clock::now() < deadline);
		Tests::Check(!backend.GetDesktopRenderStatus().failed, "Desktop rendering remains healthy");
		Tests::Check(backend.GetDesktopRenderStatus().suspended ||
		    backend.GetDesktopRenderStatus().presentedFrameCount > before, "Desktop presentation eventually makes progress");
	};
	const auto checkSize = [&](std::uint32_t width, std::uint32_t height)
	{
		const auto status = backend.GetDesktopRenderStatus();
		Tests::Check(status.width == width && status.height == height && !status.suspended,
		    "Actual swapchain resource dimensions follow the client pixels");
		Tests::Check(status.sampleCount == 1 || status.sampleCount == 2 || status.sampleCount == 4,
		    "Report the selected desktop scene sample count");
		const std::wstring extent = L"desktop:" + std::to_wstring(width) + L"x" + std::to_wstring(height);
		Tests::Check(backend.GetBackendName().find(extent) != std::wstring_view::npos,
		    "Backend diagnostics report the actual desktop extent");
	};
	renderFrame();
	checkSize(640, 360);
	renderFrame();
	const auto warmAtlas = backend.GetDesktopRenderStatus();
	Tests::Check(warmAtlas.atlasGlyphCount > 0 && warmAtlas.atlasLayoutCount > 0,
	    "Production board rendering populates the glyph atlas and text layout cache");
	Tests::Check(warmAtlas.atlasVertexCount > 0 && warmAtlas.atlasVertexCount % 3 == 0,
	    "Production board text emits nonempty complete atlas triangles");
	Tests::Check(warmAtlas.atlasUploadCount == 2,
	    "Both independently in-flight desktop pipelines upload their initial atlas snapshot");
	for (int frame = 0; frame < 5; ++frame)
	{
		renderFrame();
		const auto status = backend.GetDesktopRenderStatus();
		Tests::Check(status.atlasUploadCount == warmAtlas.atlasUploadCount &&
		    status.atlasGlyphCount == warmAtlas.atlasGlyphCount,
		    "Recurring static board text does not upload or rasterize its atlas again");
	}
	// These CJK glyphs do not occur in the initial statistics strings. The first
	// pipeline commits the shared CPU dirty range; the other must still catch up.
	field.statistics.pitchName = L"新球種・薔薇麒麟鷹";
	backend.SetFieldPresentationState(field);
	renderFrame();
	const auto changedAtlas = backend.GetDesktopRenderStatus();
	Tests::Check(changedAtlas.atlasGlyphCount > warmAtlas.atlasGlyphCount &&
	    changedAtlas.atlasUploadCount == warmAtlas.atlasUploadCount + 1,
	    "A new Japanese pitch name generates glyphs and updates the current GPU atlas");
	renderFrame();
	const auto synchronizedAtlas = backend.GetDesktopRenderStatus();
	Tests::Check(synchronizedAtlas.atlasGlyphCount == changedAtlas.atlasGlyphCount &&
	    synchronizedAtlas.atlasUploadCount == warmAtlas.atlasUploadCount + 2,
	    "The other frame pipeline uploads new glyphs after the first commits the dirty range");
	renderFrame();
	Tests::Check(backend.GetDesktopRenderStatus().atlasUploadCount == synchronizedAtlas.atlasUploadCount,
	    "Both synchronized GPU atlas instances stop uploading unchanged text");
	const auto beforeBlockedMirror = backend.GetDesktopRenderStatus();
	{
		const auto graphics = backend.GetGraphicsBinding();
		Tests::GpuQueueGate gate(graphics.device, graphics.queue);
		for (int frame = 0; frame < 6; ++frame)
		{
			backend.BeginFrame();
			backend.RenderSimulation({});
			backend.EndFrame();
		}
		gate.Unlock();
		Tests::Check(!gate.timedOut, "A busy desktop GPU/presentation never blocks the XR application thread");
	}
	const auto blockedMirror = backend.GetDesktopRenderStatus();
	Tests::Check(blockedMirror.skippedMirrorFrameCount > beforeBlockedMirror.skippedMirrorFrameCount,
	    "Busy mirror frames are skipped rather than delaying HMD rendering");
	Tests::Check(blockedMirror.width == beforeBlockedMirror.width && blockedMirror.height == beforeBlockedMirror.height &&
	    blockedMirror.sampleCount == beforeBlockedMirror.sampleCount, "Nonblocking mirror preserves extent and antialiasing quality");
	renderFrame();
	for (const auto dimensions : std::array{
	    std::array<LONG, 2>{ 320, 240 }, std::array<LONG, 2>{ 1280, 720 },
	    std::array<LONG, 2>{ 401, 813 }, std::array<LONG, 2>{ 1920, 1080 },
	    std::array<LONG, 2>{ 640, 360 } })
	{
		window.ResizeClient(dimensions[0], dimensions[1]);
		renderFrame();
		renderFrame();
		checkSize(static_cast<std::uint32_t>(dimensions[0]), static_cast<std::uint32_t>(dimensions[1]));
		Tests::Check(backend.GetDesktopRenderStatus().atlasUploadCount == synchronizedAtlas.atlasUploadCount,
		    "Desktop extent changes preserve the already synchronized glyph textures");
	}
	// Toggle the minimized state without making this test window visible.
	const LONG_PTR originalStyle = GetWindowLongPtrW(window.handle, GWL_STYLE);
	SetWindowLongPtrW(window.handle, GWL_STYLE, originalStyle | WS_MINIMIZE);
	Tests::Check(IsIconic(window.handle) != FALSE, "Enter the minimized hidden-window state");
	renderFrame();
	Tests::Check(backend.GetDesktopRenderStatus().suspended, "Minimized desktop rendering is suspended");
	SetWindowLongPtrW(window.handle, GWL_STYLE, originalStyle);
	window.ResizeClient(800, 450);
	renderFrame();
	checkSize(800, 450);
	Tests::Check(IsWindowVisible(window.handle) == FALSE, "Integration test never displays its native window");
	{
		BaseballSimulatorVR::Common::OverlayVisibilitySettings hidden{};
		hidden.showFloorMesh = hidden.showBattedTrajectory = false;
		hidden.strikeZoneDisplayMode = BaseballSimulatorVR::Common::StrikeZoneDisplayMode::None;
		hidden.pitchTrajectoryDisplayMode = BaseballSimulatorVR::Common::PitchTrajectoryDisplayMode::None;
		backend.SetOverlayVisibility(hidden);
		backend.SetFieldPresentationState({});
		auto away = mirrorView;
		away.position.z = -50.0;
		backend.SetDesktopMirrorView(away);
		renderFrame();
		Tests::Check(backend.GetBackendName().find(L"dp:empty-world-shader") != std::wstring_view::npos &&
		    backend.GetDesktopRenderStatus().atlasVertexCount == 0,
		    "Looking away from a world path never reprojects it into a different legacy chart or center reticle");
	}
	Tests::Check(DestroyWindow(window.handle) != FALSE, "Remove the desktop target window");
	window.handle = nullptr;
	backend.BeginFrame();
	backend.RenderSimulation({});
	backend.EndFrame();
	Tests::Check(backend.GetDesktopRenderStatus().failed && backend.GetDesktopRenderStatus().suspended,
	    "A lost desktop target latches failure instead of drawing an outdated buffer");
	backend.BeginFrame();
	backend.EndFrame();
	Tests::Check(backend.GetDesktopRenderStatus().failed, "Failed desktop rendering does not silently resume");
	backend.Shutdown();
	Tests::Check(backend.GetDesktopRenderStatus().width == 0 &&
	    backend.GetDesktopRenderStatus().height == 0, "Shutdown clears actual render extent");
	if (informationQueue)
	{
		for (UINT64 index = 0; index < informationQueue->GetNumStoredMessages(); ++index)
		{
			SIZE_T messageSize = 0;
			Tests::Check(SUCCEEDED(informationQueue->GetMessage(index, nullptr, &messageSize)),
			    "Read D3D12 debug message size");
			std::vector<std::byte> storage(messageSize);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			Tests::Check(SUCCEEDED(informationQueue->GetMessage(index, message, &messageSize)),
			    "Read D3D12 debug message");
			if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
			    message->Severity == D3D12_MESSAGE_SEVERITY_ERROR)
				Tests::Check(false, message->pDescription);
		}
	}
#else
	std::cout << "SKIP desktop resize: DX12 backend requires 64-bit Windows\n";
#endif
}

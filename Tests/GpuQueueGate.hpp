#pragma once

#include "../BaseballSimulatorVR/Common/D3d12RenderFrame.hpp"
#include "TestSupport.hpp"
#include <atomic>
#include <thread>

namespace Tests
{
// Gate the GPU queue; a watchdog releases it if CPU submission blocks.
class GpuQueueGate final
{
  public:
	GpuQueueGate(ID3D12Device *device, ID3D12CommandQueue *target) : queue(target)
	{
		Tests::Check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))),
					 "Create GPU queue gate");
		Tests::Check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed))),
					 "Create per-view submission fence");
		cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		Tests::Check(cancel && event, "Create queue synchronization test events");
		Tests::Check(SUCCEEDED(queue->Wait(gate.Get(), 1)), "Block the GPU without blocking the CPU");
		watchdog = std::jthread(
			[this]
			{
				// One second, times BASEBALLSIM_TEST_TIMEOUT_SCALE for slow CI/sanitizer runs.
				if (WaitForSingleObject(cancel, Tests::ScaleTimeoutMilliseconds(1000)) == WAIT_TIMEOUT)
				{
					timedOut = true;
					(void)gate->Signal(1);
				}
			});
	}
	~GpuQueueGate()
	{
		Unlock();
		// Keep test resources alive even when a Check throws.
		if (SUCCEEDED(queue->Signal(completed.Get(), 1000)))
			(void)BaseballSimulatorVR::Common::D3d12RenderFrame::WaitForFence(completed.Get(), 1000, event);
		CloseHandle(cancel);
		CloseHandle(event);
	}
	void Unlock()
	{
		(void)gate->Signal(1);
		SetEvent(cancel);
		if (watchdog.joinable())
			watchdog.join();
	}
	Microsoft::WRL::ComPtr<ID3D12Fence> gate, completed;
	HANDLE event{};
	std::atomic<bool> timedOut{false};

  private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
	HANDLE cancel{};
	std::jthread watchdog;
};
} // namespace Tests

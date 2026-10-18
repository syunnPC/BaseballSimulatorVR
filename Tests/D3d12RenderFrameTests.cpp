#include "../BaseballSimulatorVR/Common/D3d12RenderFrame.hpp"
#include "GpuQueueGate.hpp"
#include "GpuTestContext.hpp"

#include <atomic>
#include <future>
#include <thread>

void RunD3d12RenderFrameTests()
{
	using BaseballSimulatorVR::Common::D3d12RenderFrame;
	Tests::GpuTestContext gpu(32, 32);
	std::array<D3d12RenderFrame, 2> frames;
	for (auto &frame : frames)
		Tests::Check(SUCCEEDED(frame.Initialize(gpu.device.Get(), gpu.ColorFormat, gpu.DepthFormat, 1, 32)),
					 "Initialize independent production per-view resources");
	Tests::Check(frames[0].commands != frames[1].commands && frames[0].allocator != frames[1].allocator &&
					 frames[0].vertices != frames[1].vertices,
				 "Views never share writable command or upload resources");
	D3D12_RESOURCE_DESC description{};
	description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	description.Width = 256;
	description.Height = 1;
	description.DepthOrArraySize = 1;
	description.MipLevels = 1;
	description.SampleDesc.Count = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	Microsoft::WRL::ComPtr<ID3D12Resource> readback;
	Tests::Check(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
															   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
															   IID_PPV_ARGS(&readback))),
				 "Create two-view GPU result readback");
	Tests::GpuQueueGate gate(gpu.device.Get(), gpu.queue.Get());
	const std::array<std::uint32_t, 2> expected{0x12345678, 0xABCDEF90};
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		auto &frame = frames[i];
		Tests::Check(SUCCEEDED(frame.Begin(gate.completed.Get(), gate.event)),
					 "First view use needs no fence wait");
		void *mapped{};
		const D3D12_RANGE noRead{0, 0};
		Tests::Check(SUCCEEDED(frame.vertices->Map(0, &noRead, &mapped)), "Map independent eye upload");
		std::memcpy(mapped, &expected[i], sizeof(expected[i]));
		const D3D12_RANGE written{0, sizeof(expected[i])};
		frame.vertices->Unmap(0, &written);
		frame.commands->CopyBufferRegion(readback.Get(), i * sizeof(expected[i]), frame.vertices.Get(), 0,
										 sizeof(expected[i]));
		Tests::Check(SUCCEEDED(frame.Submit(gpu.queue.Get(), gate.completed.Get(), i + 1)),
					 "Submit view without waiting for GPU execution");
	}
	const bool submittedBeforeGpuStarted = !gate.timedOut && gate.completed->GetCompletedValue() == 0;
	auto reuse =
		std::async(std::launch::async, [&] { return frames[0].Begin(gate.completed.Get(), gate.event); });
	const bool reuseWaits = reuse.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
	gate.Unlock();
	Tests::Check(SUCCEEDED(reuse.get()), "A view becomes reusable after its submitted fence completes");
	Tests::Check(SUCCEEDED(frames[0].commands->Close()), "Close empty reused test command list");
	Tests::Check(SUCCEEDED(D3d12RenderFrame::WaitForFence(gate.completed.Get(), 2, gate.event)),
				 "Drain both submitted views before readback");
	Tests::Check(submittedBeforeGpuStarted,
				 "Both eyes submit while GPU is blocked; there is no per-eye CPU completion wait");
	Tests::Check(reuseWaits, "Reusing an in-flight view waits for its own GPU fence");
	void *mapped{};
	const D3D12_RANGE range{0, sizeof(expected)};
	Tests::Check(SUCCEEDED(readback->Map(0, &range, &mapped)), "Map independently submitted eye results");
	std::array<std::uint32_t, 2> actual{};
	std::memcpy(actual.data(), mapped, sizeof(actual));
	const D3D12_RANGE noWrite{0, 0};
	readback->Unmap(0, &noWrite);
	Tests::Check(actual == expected, "Both in-flight eye uploads survive without cross-eye overwrite");
}

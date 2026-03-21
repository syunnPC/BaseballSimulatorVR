#pragma once

#if defined(_WIN64)
#include <d3d12.h>
#endif

namespace BaseballSimulatorVR::Common
{
	struct GraphicsBinding final
	{
#if defined(_WIN64)
		ID3D12Device* device{ nullptr };
		ID3D12CommandQueue* queue{ nullptr };
#else
		void* device{ nullptr };
		void* queue{ nullptr };
#endif

		[[nodiscard]] bool HasDevice() const noexcept
		{
			return device != nullptr;
		}

		[[nodiscard]] bool HasQueue() const noexcept
		{
			return queue != nullptr;
		}

		[[nodiscard]] bool IsComplete() const noexcept
		{
			return HasDevice() && HasQueue();
		}
	};
} // namespace BaseballSimulatorVR::Common

#pragma once

#include "VkdAmd/Wddm/WddmAdapter.hpp"

namespace vkd::wddm
{
	// Node types reported by DxgKrnl for this adapter: node 0 = 3D, node 1 = COPY (SDMA, refuses
	// HwQueueSupported contexts), nodes 2..7 = OTHER (compute). Family 0 (graphics) runs on the 3D
	// node; families 1 (compute) and 2 (transfer) both run on node 2, the compute engine that has a
	// working hw queue and executes CP packets -- node 1 would only take SDMA packets.
	inline uint32_t QueueFamilyToNodeOrdinal(uint32_t queueFamilyIndex)
	{
		return queueFamilyIndex == 0 ? 0 : 2;
	}

	class WddmContext
	{
	public:
		WddmContext() = default;
		~WddmContext();

		WddmContext(const WddmContext&) = delete;
		WddmContext& operator=(const WddmContext&) = delete;
		WddmContext(WddmContext&& other) noexcept;
		WddmContext& operator=(WddmContext&& other) noexcept;

		NTSTATUS Create(D3DKMT_HANDLE device, uint32_t nodeOrdinal,
						const void* privData, uint32_t privDataSize);
		void Destroy();

		NTSTATUS Submit(uint64_t commandGpuVa, uint32_t commandLength, const void* privData, uint32_t privDataSize);
		NTSTATUS WaitForProgressFence() const;

		[[nodiscard]] bool IsValid() const
		{
			return m_context != 0;
		}
		[[nodiscard]] D3DKMT_HANDLE GetHandle() const
		{
			return m_context;
		}
		[[nodiscard]] D3DKMT_HANDLE GetHwQueue() const
		{
			return m_hwQueue;
		}
		[[nodiscard]] D3DKMT_HANDLE GetHwQueueProgressFence() const
		{
			return m_hwQueueFence;
		}
		[[nodiscard]] uint64_t GetSubmitFenceId() const
		{
			return m_submitFenceId;
		}

	private:
		D3DKMT_HANDLE m_device = 0;
		D3DKMT_HANDLE m_context = 0;
		D3DKMT_HANDLE m_hwQueue = 0;
		D3DKMT_HANDLE m_hwQueueFence = 0;
		uint64_t m_submitFenceId = 0;
	};
} // namespace vkd::wddm

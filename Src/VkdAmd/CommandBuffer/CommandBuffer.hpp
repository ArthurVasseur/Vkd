#pragma once

#include <span>

#include "Vkd/CommandBuffer/CommandBuffer.hpp"

#include "VkdAmd/Wddm/WddmAllocation.hpp"

namespace vkd::amd
{
	class CommandBuffer;

	// Runs the host-visible effects of a command buffer (and its secondaries), in order, once its GPU
	// work completed (submits are synchronous): vkCmdSetEvent/vkCmdResetEvent, and the query ops --
	// resets, begin/end (availability), timestamps (the GPU clock values the IB wrote), copies of
	// results into buffers.
	void ApplyHostOps(const CommandBuffer& cmd);
	bool NeedsDescriptorArena(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth = 0);
	uint64_t DescriptorTableCount(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth = 0);
	bool NeedsAttributeRing(std::span<const vkd::CommandBuffer::Op> ops, uint32_t depth = 0);

	class CommandBuffer : public vkd::CommandBuffer
	{
	public:
		using vkd::CommandBuffer::CommandBuffer;
		~CommandBuffer() override = default;

		VkResult Create(vkd::CommandPool& owner, VkCommandBufferLevel level) override;

		VkResult Bake();

		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetGpuVa() const
		{
			return m_ib.GetGpuVa();
		}
		// Queue families 1/2 run on the compute engine (node 2).
		[[nodiscard]] bool IsComputeEngine() const;
		[[nodiscard]] uint32_t GetCommandLength() const
		{
			return m_commandLength;
		}
		[[nodiscard]] const void* GetMappedIb() const
		{
			return m_mappedIb;
		}
		// GPU clock values of the command buffer's vkCmdWriteTimestamp, in recording order.
		[[nodiscard]] const uint64_t* GetTimestamps() const
		{
			return m_timestampValues;
		}
		[[nodiscard]] uint32_t GetTimestampCount() const
		{
			return m_timestampCount;
		}
		// DB z-pass counters of the command buffer's occlusion queries: one slot of
		// Pm4Stream::kOcclusionSlotBytes per vkCmdBeginQuery, in recording order.
		[[nodiscard]] const uint64_t* GetOcclusionData() const
		{
			return m_occlusionValues;
		}
		[[nodiscard]] uint32_t GetOcclusionCount() const
		{
			return m_occlusionCount;
		}

	private:
		// Encodes every op into the current IB; VK_ERROR_OUT_OF_DEVICE_MEMORY when it doesn't fit.
		VkResult Encode();

		wddm::WddmAllocation m_ib;
		wddm::WddmAllocation m_descriptorTables; // dispatch descriptor tables (shader VA window)
		wddm::WddmAllocation m_queryData; // timestamps (8 bytes each), then occlusion slots
		const uint64_t* m_timestampValues = nullptr;
		uint32_t m_timestampCount = 0;
		const uint64_t* m_occlusionValues = nullptr;
		uint32_t m_occlusionCount = 0;
		const void* m_mappedIb = nullptr;
		uint32_t m_commandLength = 0;
		bool m_baked = false;
	};
} // namespace vkd::amd

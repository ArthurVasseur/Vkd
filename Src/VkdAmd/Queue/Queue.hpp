/**
 * @file Queue.hpp
 * @brief AMD queue backed by D3DKMTCreateContextVirtual
 * @date 2026-08-31
 *
 * Queue objects are built in amdvlk64 FUN_18025c900 (called from vkCreateDevice).
 * Submit is D3DKMTSubmitCommandToHwQueue (PAL FUN_18053E8C0 / pfn+0x2928) with
 * the 200 B blob. WaitIdle waits the HwQueue progress fence.
 */

#pragma once

#include "Vkd/Queue/Queue.hpp"

#include "VkdAmd/PalQueueCreateInfo.hpp"
#include "VkdAmd/Wddm/WddmAllocation.hpp"
#include "VkdAmd/Wddm/WddmContext.hpp"

namespace vkd::amd
{
	class AmdDevice;
	class CommandBuffer;

	class Queue : public vkd::Queue
	{
	public:
		using vkd::Queue::Queue;
		~Queue() override = default;

		VkResult Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) override;

		[[nodiscard]] const wddm::WddmContext& GetContext() const
		{
			return m_context;
		}
		[[nodiscard]] const PalQueueCreateInfo& GetPalCreateInfo() const
		{
			return m_palInfo;
		}

	protected:
		VkResult Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) override;
		VkResult WaitIdle() override;
		VkResult BindSparse(uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) override;

	private:
		VkResult CreateSubmitIbs(AmdDevice& device);
		VkResult InitStateShadowing();
		// False (and cmd's IB dumped to %TEMP%) when the device left the ACTIVE execution state.
		bool CheckDeviceState(const CommandBuffer* cmd) const;

		PalQueueCreateInfo m_palInfo{};
		wddm::WddmContext m_context;
		// Per-queue IBs referenced by every submit's private data: the preamble (record 0) and the
		// EOP postamble (record 2), plus the GPU-writable semaphore the postamble writes.
		wddm::WddmAllocation m_submitIbs;
		wddm::WddmAllocation m_semaphore;
		uint32_t m_preambleBytes = 0;
		// Graphics queues: the register mirror of state shadowing (Pm4.hpp, kShadowMemoryBytes) and
		// the length of the shadow-init preamble in m_submitIbs.
		bool m_stateShadowing = false;
		wddm::WddmAllocation m_shadow;
		uint32_t m_shadowInitBytes = 0;
	};
} // namespace vkd::amd

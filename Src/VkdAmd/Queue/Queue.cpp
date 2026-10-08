/**
 * @file Queue.cpp
 * @brief AMD queue — WDDM context + D3DKMTSubmitCommand
 * @date 2026-08-31
 */

#include "VkdAmd/Queue/Queue.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Concerto/Core/Logger/Logger.hpp>

#include "Vkd/Synchronization/Semaphore/Semaphore.hpp"

#include "VkdAmd/CommandBuffer/CommandBuffer.hpp"
#include "VkdAmd/ContextPrivateData.hpp"
#include "VkdAmd/Device/Device.hpp"
#include "VkdAmd/Fence/Fence.hpp"
#include "VkdAmd/Pm4.hpp"
#include "VkdAmd/PrivateData.hpp"
#include "VkdAmd/SubmitPrivateData.hpp"

namespace vkd::amd
{
	namespace
	{
		// Layout of m_submitIbs (written once at queue creation).
		constexpr uint32_t kPreambleOffset = 0x000; // per-submit preamble
		constexpr uint32_t kPreambleCapacity = 0x400;
		constexpr uint32_t kPostambleOffset = 0x400;
		constexpr uint32_t kNopIbOffset = 0x480; // main IB of the shadow-init submit
		constexpr uint32_t kNopIbBytes = 0x20;
		constexpr uint32_t kShadowInitOffset = 0x1000; // first submit of a graphics queue
		constexpr uint32_t kShadowInitCapacity = 0x1000;
		constexpr uint32_t kSubmitIbsBytes = kShadowInitOffset + kShadowInitCapacity;

		// VKD_AMD_STATE_SHADOWING=0: graphics queues without register shadowing, as before it was
		// implemented (no preemption safety), to tell a shadowing problem apart.
		bool StateShadowingDisabled()
		{
			static const bool disabled = []
			{
				char value[8] = {};
				size_t length = 0;
				return getenv_s(&length, value, sizeof(value), "VKD_AMD_STATE_SHADOWING") == 0 && length > 1 && value[0] == '0';
			}();
			return disabled;
		}
	} // namespace

	VkResult Queue::CreateSubmitIbs(AmdDevice& device)
	{
		if (!device.AllocateGpuMemory(0x10000, kSurfaceFlagsInitB, m_submitIbs) || m_submitIbs.GetGpuVa() == 0)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		// Like AMDVLK's semaphore allocation: host-visible, GPU-writable (command-buffer
		// allocations are only ever read by the GPU).
		if (!device.AllocateGpuMemory(0x10000, kSurfaceFlagsHostVisible, m_semaphore) || m_semaphore.GetGpuVa() == 0)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		// The graphics preamble waits for it to be 0 (the previous submit's postamble done): the
		// first submit must not wait.
		void* semaphore = m_semaphore.MapToHost();
		if (!semaphore)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		std::memset(semaphore, 0, 0x1000);
		m_semaphore.UnmapFromHost();

		// Graphics queues shadow their registers, as AMDVLK's (compute queues rely on the
		// firmware's compute wave save/restore). The CP writes the mirror, so it starts zeroed.
		const bool graphics = GetQueueFamilyIndex() == 0;
		m_stateShadowing = graphics && !StateShadowingDisabled();
		if (m_stateShadowing)
		{
			if (!device.AllocateGpuMemory(kShadowMemoryBytes, kSurfaceFlagsHostVisible, m_shadow) || m_shadow.GetGpuVa() == 0)
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			void* shadow = m_shadow.MapToHost();
			if (!shadow)
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			std::memset(shadow, 0, kShadowMemoryBytes);
			m_shadow.UnmapFromHost();
		}

		auto* base = static_cast<uint8_t*>(m_submitIbs.MapToHost());
		if (!base)
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		std::memset(base, 0, kSubmitIbsBytes);

		// The preamble record (flags 0xC) of every submit, also what the CP replays when the queue
		// resumes from a preemption. Graphics: the state-shadowing preamble, or CONTEXT_CONTROL with
		// shadowing off; compute: NOPs.
		Pm4Stream preamble{reinterpret_cast<uint32_t*>(base + kPreambleOffset), kPreambleCapacity / sizeof(uint32_t)};
		if (m_stateShadowing)
			preamble.GfxSubmitPreamble(m_shadow.GetGpuVa(), m_semaphore.GetGpuVa());
		else if (graphics)
			preamble.ContextControlNoShadowing();
		if (preamble.count == 0)
			preamble.NopBlock(8);
		preamble.PadTo8Dw();
		if (preamble.count == preamble.capacity)
			return VK_ERROR_INITIALIZATION_FAILED;
		m_preambleBytes = preamble.Bytes();

		if (m_stateShadowing)
		{
			Pm4Stream init{reinterpret_cast<uint32_t*>(base + kShadowInitOffset), kShadowInitCapacity / sizeof(uint32_t)};
			init.GfxShadowInit(m_shadow.GetGpuVa(), m_semaphore.GetGpuVa());
			init.PadTo8Dw();
			if (init.count == init.capacity)
				return VK_ERROR_INITIALIZATION_FAILED;
			m_shadowInitBytes = init.Bytes();

			Pm4Stream nop{reinterpret_cast<uint32_t*>(base + kNopIbOffset), kNopIbBytes / sizeof(uint32_t)};
			nop.NopBlock(nop.capacity);
		}

		Pm4Stream postamble;
		postamble.dw = reinterpret_cast<uint32_t*>(base + kPostambleOffset);
		postamble.capacity = kSubmitPostambleBytes / sizeof(uint32_t);
		postamble.SubmitPostamble(m_semaphore.GetGpuVa());
		if (postamble.Bytes() != kSubmitPostambleBytes)
			return VK_ERROR_INITIALIZATION_FAILED;

		// Stays CPU-mapped for the queue's lifetime, as AMDVLK keeps its IB allocations.
		return VK_SUCCESS;
	}

	// AMDVLK's queue-init submit (wddm-dump [submit-records][re]): the shadow-init preamble, with a
	// NOP IB in place of a command buffer. Kept apart from the command buffers so that a preemption
	// never replays the initialization over the state they shadowed.
	VkResult Queue::InitStateShadowing()
	{
		const uint64_t ibs = m_submitIbs.GetGpuVa();
		const SubmitIb init{ibs + kShadowInitOffset, m_shadowInitBytes};
		const SubmitPrivateData priv = MakeSubmitPrivateData(init,
															 SubmitIb{ibs + kNopIbOffset, kNopIbBytes},
															 SubmitIb{ibs + kPostambleOffset, kSubmitPostambleBytes});
		const NTSTATUS st = m_context.Submit(init.gpuVa, init.length, &priv, sizeof(priv));
		if (st != STATUS_SUCCESS)
		{
			cct::Logger::Error("AmdQueue: state-shadowing init submit NTSTATUS=0x{:08X}", static_cast<uint32_t>(st));
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		const VkResult idle = WaitIdle();
		if (idle != VK_SUCCESS)
			return idle;
		return CheckDeviceState(nullptr) ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
	}

	VkResult Queue::Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		VkResult base = vkd::Queue::Create(owner, queueFamilyIndex, queueIndex, flags);
		if (base != VK_SUCCESS)
			return base;

		auto* amdDevice = static_cast<AmdDevice*>(&owner);
		m_palInfo = FillPalQueueCreateInfo(queueFamilyIndex, queueIndex);
		uint32_t node = wddm::QueueFamilyToNodeOrdinal(queueFamilyIndex);
		const ContextPrivateData privData;
		const D3DKMT_HANDLE hDevice = amdDevice->GetWddmDevice().GetDeviceHandle();

		NTSTATUS ctxStatus = m_context.Create(hDevice, node, &privData, sizeof(privData));

		if (ctxStatus != 0)
		{
			cct::Logger::Error("AmdQueue: D3DKMTCreateContextVirtual failed family={} NTSTATUS=0x{:08X}",
							   queueFamilyIndex, static_cast<uint32_t>(ctxStatus));
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		const VkResult ibs = CreateSubmitIbs(*amdDevice);
		if (ibs != VK_SUCCESS)
		{
			cct::Logger::Error("AmdQueue: failed to create the per-queue submit IBs");
			return ibs;
		}
		if (m_stateShadowing)
		{
			const VkResult shadowing = InitStateShadowing();
			if (shadowing != VK_SUCCESS)
				return shadowing;
		}

		cct::Logger::Info("AmdQueue: context created family={} palType={} engineType={} node={} hCtx=0x{:X}",
						  queueFamilyIndex,
						  static_cast<uint32_t>(m_palInfo.queueType),
						  static_cast<uint32_t>(m_palInfo.engineType),
						  node,
						  static_cast<uint64_t>(m_context.GetHandle()));
		return VK_SUCCESS;
	}

	VkResult Queue::Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
	{
		if (!m_context.IsValid())
			return VK_ERROR_INITIALIZATION_FAILED;
		if (submitCount == 0)
		{
			if (fence)
			{
				VKD_FROM_HANDLE(vkd::Fence, fenceObj, fence);
				static_cast<Fence*>(fenceObj)->Signal();
			}
			return VK_SUCCESS;
		}
		if (!pSubmits)
			return VK_ERROR_INITIALIZATION_FAILED;

		for (uint32_t s = 0; s < submitCount; ++s)
		{
			// Every earlier submission has completed, so a wait semaphore is signaled by now unless it
			// comes from vkAcquireNextImageKHR or another queue: wait for it on the CPU.
			for (uint32_t i = 0; i < pSubmits[s].waitSemaphoreCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::Semaphore, semaphoreObj, pSubmits[s].pWaitSemaphores[i]);
				semaphoreObj->Wait();
			}
			for (uint32_t i = 0; i < pSubmits[s].commandBufferCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::CommandBuffer, cmdObj, pSubmits[s].pCommandBuffers[i]);
				auto* cmd = static_cast<CommandBuffer*>(cmdObj);

				const VkResult mark = cmd->MarkSubmitted();
				if (mark != VK_SUCCESS)
					return mark;

				// Bake() emits CP PM4, which needs a node with a hw queue (see QueueFamilyToNodeOrdinal:
				// node 1, the COPY/SDMA engine, refuses HwQueueSupported contexts and can't parse CP
				// packets). If a context ever ends up without a hw queue, drop the work rather than
				// hand those packets to the legacy submit path.
				if (m_context.GetHwQueue() == 0)
				{
					static bool warned = false;
					if (!warned)
					{
						warned = true;
						cct::Logger::Warning("AmdQueue: family {} has no hw queue, command buffers are not executed on the GPU",
											 GetQueueFamilyIndex());
					}
					ApplyHostOps(*cmd);
					cmd->MarkComplete();
					continue;
				}

				const VkResult bake = cmd->Bake();
				if (bake != VK_SUCCESS)
				{
					cmd->MarkComplete();
					return bake;
				}

				// Records as AMDVLK and RADV lay them out: the preamble (flags 0xC, also the D3DKMT
				// CommandBuffer), the command buffer as the main IB, the postamble.
				const SubmitIb preamble{m_submitIbs.GetGpuVa() + kPreambleOffset, m_preambleBytes};
				const SubmitPrivateData priv = MakeSubmitPrivateData(
					preamble,
					SubmitIb{cmd->GetGpuVa(), cmd->GetCommandLength()},
					SubmitIb{m_submitIbs.GetGpuVa() + kPostambleOffset, kSubmitPostambleBytes});
				const NTSTATUS st = m_context.Submit(preamble.gpuVa, preamble.length, &priv, sizeof(priv));
				if (st != STATUS_SUCCESS)
				{
					cmd->MarkComplete();
					cct::Logger::Error("AmdQueue: SubmitToHwQueue NTSTATUS=0x{:08X} va=0x{:X} len={} (fence {})",
									   static_cast<uint32_t>(st),
									   static_cast<uint64_t>(cmd->GetGpuVa()),
									   cmd->GetCommandLength(), m_context.GetSubmitFenceId());
					return VK_ERROR_DEVICE_LOST;
				}

				// Wait per command buffer and check the device state: a GPU fault or a rejected submit
				// only shows up asynchronously (the next call fails with 0xC01E0200), so catch it here,
				// on the command buffer that caused it, and keep its IB for post-mortem.
				const VkResult idle = WaitIdle();
				if (idle != VK_SUCCESS || !CheckDeviceState(cmd))
				{
					cmd->MarkComplete();
					return VK_ERROR_DEVICE_LOST;
				}
				// Before MarkComplete: it drops the ops of a one-time-submit command buffer.
				ApplyHostOps(*cmd);
				cmd->MarkComplete();
			}
			for (uint32_t i = 0; i < pSubmits[s].signalSemaphoreCount; ++i)
			{
				VKD_FROM_HANDLE(vkd::Semaphore, semaphoreObj, pSubmits[s].pSignalSemaphores[i]);
				semaphoreObj->Signal();
			}
		}

		const VkResult idle = WaitIdle();
		if (idle != VK_SUCCESS)
			return idle;

		if (fence)
		{
			VKD_FROM_HANDLE(vkd::Fence, fenceObj, fence);
			static_cast<Fence*>(fenceObj)->Signal();
		}
		return VK_SUCCESS;
	}

	bool Queue::CheckDeviceState(const CommandBuffer* cmd) const
	{
		auto* amdDevice = static_cast<AmdDevice*>(GetOwner());
		D3DKMT_GETDEVICESTATE state = {};
		state.hDevice = amdDevice->GetWddmDevice().GetDeviceHandle();
		state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
		if (D3DKMTGetDeviceState(&state) != STATUS_SUCCESS || state.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE)
			return true;

		// ExecutionState: 2=RESET 3=HUNG 4=STOPPED 5=OOM 6=DMAFAULT 7=DMAPAGEFAULT
		if (cmd)
		{
			std::string ops;
			for (const vkd::CommandBuffer::Op& op : cmd->GetOps())
				ops += std::to_string(op.index()) + ' ';
			static int dumpIndex = 0;
			char path[MAX_PATH] = {};
			GetTempPathA(MAX_PATH, path);
			const std::string file = std::string(path) + "vkd-amd-fault-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(dumpIndex++) + ".bin";
			if (FILE* f = std::fopen(file.c_str(), "wb"))
			{
				std::fwrite(cmd->GetMappedIb(), 1, cmd->GetCommandLength(), f);
				std::fclose(f);
			}
			cct::Logger::Error("AmdQueue: device execution state {} after command buffer va=0x{:X} len={} (op indices: {}), IB dumped to {}",
							   static_cast<int>(state.ExecutionState), static_cast<uint64_t>(cmd->GetGpuVa()), cmd->GetCommandLength(), ops, file);
		}
		else
			cct::Logger::Error("AmdQueue: device execution state {} after the state-shadowing init submit", static_cast<int>(state.ExecutionState));
		if (state.ExecutionState == D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT)
		{
			// What faulted, as RADV's wddm2 winsys reports it.
			D3DKMT_GETDEVICESTATE fault = {};
			fault.hDevice = state.hDevice;
			fault.StateType = D3DKMT_DEVICESTATE_PAGE_FAULT;
			if (D3DKMTGetDeviceState(&fault) == STATUS_SUCCESS)
				cct::Logger::Error("AmdQueue: page fault at VA 0x{:X}, flags 0x{:X}, error {} (device-specific {}), pipeline stage {}",
								   static_cast<uint64_t>(fault.PageFaultState.FaultedVirtualAddress),
								   static_cast<uint32_t>(fault.PageFaultState.PageFaultFlags),
								   static_cast<int>(fault.PageFaultState.FaultErrorCode.GeneralErrorCode),
								   static_cast<int>(fault.PageFaultState.FaultErrorCode.DeviceSpecificCode),
								   static_cast<int>(fault.PageFaultState.FaultedPipelineStage));
		}
		return false;
	}

	VkResult Queue::WaitIdle()
	{
		auto* amdDevice = static_cast<AmdDevice*>(GetOwner());
		const NTSTATUS st = m_context.WaitForProgressFence();
		if (st != STATUS_SUCCESS)
		{
			cct::Logger::Error("AmdQueue: WaitForProgressFence NTSTATUS=0x{:08X}", static_cast<uint32_t>(st));
			return VK_ERROR_DEVICE_LOST;
		}
		return VK_SUCCESS;
	}

	VkResult Queue::BindSparse(uint32_t /*bindInfoCount*/, const VkBindSparseInfo* /*pBindInfo*/, VkFence /*fence*/)
	{
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
} // namespace vkd::amd

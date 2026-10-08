#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "VkdAmd/ShadowedRegisters.hpp"

namespace vkd::amd
{
	// GFX9+/GFX11 PM4. COUNT = total DWORDs of the packet minus 2 (PAL Type3Header).
	inline constexpr uint32_t PkT3(uint32_t op, uint32_t count)
	{
		return (3u << 30) | ((count & 0x3FFFu) << 16) | ((op & 0xFFu) << 8);
	}

	inline constexpr uint32_t kItNop = 0x10;
	inline constexpr uint32_t kItClearState = 0x12;
	inline constexpr uint32_t kItContextControl = 0x28;
	inline constexpr uint32_t kItDrawIndexAuto = 0x2D;
	inline constexpr uint32_t kItDrawIndex2 = 0x27;
	inline constexpr uint32_t kItNumInstances = 0x2F;
	inline constexpr uint32_t kItSetContextReg = 0x69;
	inline constexpr uint32_t kItSetUconfigReg = 0x79;
	inline constexpr uint32_t kItSetUconfigRegIndex = 0x7A;
	inline constexpr uint32_t kContextRegBase = 0xA000;
	inline constexpr uint32_t kUconfigRegBase = 0xC000;
	inline constexpr uint32_t kItDispatchDirect = 0x15;
	inline constexpr uint32_t kItWriteData = 0x37;
	inline constexpr uint32_t kItWaitRegMem = 0x3C;
	inline constexpr uint32_t kItEventWrite = 0x46;
	inline constexpr uint32_t kItReleaseMem = 0x49;
	inline constexpr uint32_t kItDmaData = 0x50;
	inline constexpr uint32_t kItAcquireMem = 0x58;
	inline constexpr uint32_t kItLoadUconfigReg = 0x5E;
	inline constexpr uint32_t kItLoadShReg = 0x5F;
	inline constexpr uint32_t kItLoadContextReg = 0x61;
	inline constexpr uint32_t kItSetShReg = 0x76;
	inline constexpr uint32_t kItSetShRegIndex = 0x9B;

	// PM4 header SHADER_TYPE bit: set on packets that target the compute pipe (compute SH
	// registers, DISPATCH_*), as in every such packet of AMDVLK's command buffers.
	inline constexpr uint32_t kPkt3ShaderTypeCompute = 1u << 1;

	// SH register dword offsets (PAL gfx9_plus_merged_offset.h); SET_SH_REG addresses them
	// relative to 0x2C00.
	inline constexpr uint32_t kShRegBase = 0x2C00;
	inline constexpr uint32_t kComputeStartX = 0x2E04;
	inline constexpr uint32_t kComputeNumThreadX = 0x2E07;
	inline constexpr uint32_t kComputePerfcountEnable = 0x2E0B;
	inline constexpr uint32_t kComputePgmLo = 0x2E0C;
	inline constexpr uint32_t kComputePgmRsrc1 = 0x2E12;
	inline constexpr uint32_t kComputeResourceLimits = 0x2E15;
	inline constexpr uint32_t kComputeStaticThreadMgmtSe0 = 0x2E16;
	inline constexpr uint32_t kComputeTmpringSize = 0x2E18;
	inline constexpr uint32_t kComputeStaticThreadMgmtSe2 = 0x2E19;
	inline constexpr uint32_t kComputePgmRsrc3 = 0x2E28;
	inline constexpr uint32_t kComputeDispatchInterleave = 0x2E2F;
	inline constexpr uint32_t kComputeUserData0 = 0x2E40;

	// VGT_EVENT_TYPE CS_PARTIAL_FLUSH / VS_PARTIAL_FLUSH with event_index 4, VGT_FLUSH.
	inline constexpr uint32_t kEventCsPartialFlush = 0x407;
	inline constexpr uint32_t kEventVsPartialFlush = 0x40F;
	inline constexpr uint32_t kEventVgtFlush = 0x24;

	// Graphics-queue state shadowing (PAL UniversalQueueContext with mid-command-buffer preemption,
	// LOAD packets in the preamble). With CONTEXT_CONTROL's shadow enables set, the CP mirrors every
	// register write into the shadow memory -- one dword per register of the user-config, context
	// and SH spaces, each laid out as its space -- and the preamble's LOAD_*_REG packets reload it:
	// that is what restores our state when the queue resumes from a preemption.
	inline constexpr uint64_t kShadowUserConfigOffset = 0;
	inline constexpr uint64_t kShadowContextOffset = kShadowUserConfigOffset + 0x4000 * 4; // 0xC000-0xFFFF
	inline constexpr uint64_t kShadowShOffset = kShadowContextOffset + 0x2000 * 4; // 0xA000-0xBFFF
	inline constexpr uint64_t kShadowMemoryBytes = kShadowShOffset + 0x400 * 4; // 0x2C00-0x2FFF

	inline constexpr uint32_t kPkt3Nop = PkT3(kItNop, 0);

	struct Pm4Stream
	{
		uint32_t* dw = nullptr;
		uint32_t capacity = 0;
		uint32_t count = 0;
		uint64_t gpuVa = 0; // GPU VA of dw[0], for data embedded in the IB (0 = unknown)

		bool Emit(uint32_t value)
		{
			if (!dw || count >= capacity)
				return false;
			dw[count++] = value;
			return true;
		}

		[[nodiscard]] uint32_t Bytes() const
		{
			return count * 4u;
		}
		[[nodiscard]] bool HasRoom(uint32_t dwords) const
		{
			return dw && count + dwords <= capacity;
		}

		void Nop()
		{
			Emit(kPkt3Nop);
			Emit(0);
		}

		// One PKT3 NOP spanning totalDw DWORDs (header included, totalDw >= 2).
		void NopBlock(uint32_t totalDw)
		{
			Emit(PkT3(kItNop, totalDw - 2));
			for (uint32_t i = 1; i < totalDw; ++i)
				Emit(0);
		}

		// Pad with PKT3 NOP to a multiple of 8 DWORDs, like PAL's IBs. Never PKT2: AMDVLK never
		// emits it on GFX11, and every working submit on this hw queue used PKT3 NOP only.
		void PadTo8Dw()
		{
			uint32_t rem = (8 - (count & 7u)) & 7u;
			if (rem == 1)
				rem += 8; // a PKT3 NOP needs at least 2 DWORDs
			if (rem != 0)
				NopBlock(rem);
		}

		// WRITE_DATA to memory (dst_sel=5, wr_confirm). PFP is the engine of AMDVLK's submit
		// postamble; command-buffer writes use ME so they stay ordered with ME packets (DMA_DATA).
		// The caller emits the dataDw payload DWORDs right after this header.
		static constexpr uint32_t kWriteDataPfp = 0x40100500u;
		static constexpr uint32_t kWriteDataMe = 0x00100500u;

		void WriteDataHeader(uint64_t gpuVa, uint32_t dataDw, uint32_t control = kWriteDataPfp)
		{
			Emit(PkT3(kItWriteData, 2 + dataDw));
			Emit(control);
			Emit(static_cast<uint32_t>(gpuVa));
			Emit(static_cast<uint32_t>(gpuVa >> 32));
		}

		// CP DMA memory-to-memory copy (GFX9+ DMA_DATA, as RADV's si_emit_cp_dma): src/dst through
		// L2, CP_SYNC so the next packet waits for completion. Byte-granular, byteCount < 2^26.
		// Same packet shape as the DMA_DATA in AMDVLK's captured clear IB.
		void DmaCopy(uint64_t srcVa, uint64_t dstVa, uint32_t byteCount)
		{
			constexpr uint32_t kSrcAddrTcL2 = 3u << 29;
			constexpr uint32_t kDstAddrTcL2 = 3u << 20;
			constexpr uint32_t kCpSync = 1u << 31;
			Emit(PkT3(kItDmaData, 5));
			Emit(kCpSync | kSrcAddrTcL2 | kDstAddrTcL2);
			Emit(static_cast<uint32_t>(srcVa));
			Emit(static_cast<uint32_t>(srcVa >> 32));
			Emit(static_cast<uint32_t>(dstVa));
			Emit(static_cast<uint32_t>(dstVa >> 32));
			Emit(byteCount & 0x3FFFFFFu);
		}

		// CP DMA constant fill (DMA_DATA SRC_SEL=DATA: SRC_ADDR_LO is the value). dstVa and byteCount
		// must be DWORD-aligned, as RADV's si_cp_dma_clear_buffer requires.
		void DmaFill(uint64_t dstVa, uint32_t value, uint32_t byteCount)
		{
			constexpr uint32_t kSrcData = 2u << 29;
			constexpr uint32_t kDstAddrTcL2 = 3u << 20;
			constexpr uint32_t kCpSync = 1u << 31;
			Emit(PkT3(kItDmaData, 5));
			Emit(kCpSync | kSrcData | kDstAddrTcL2);
			Emit(value);
			Emit(0);
			Emit(static_cast<uint32_t>(dstVa));
			Emit(static_cast<uint32_t>(dstVa >> 32));
			Emit(byteCount & 0x3FFFFFFu);
		}

		// Embeds `bytes` of data in the IB as the payload of a NOP packet (the CP skips it) and
		// returns its GPU VA, readable by DMA_DATA. 0 if there's no room or no known IB VA.
		uint64_t EmbedData(const void* data, uint32_t bytes)
		{
			const uint32_t payloadDw = (bytes + 3) / 4;
			if (gpuVa == 0 || payloadDw == 0 || payloadDw > 0x3FFF || !HasRoom(1 + payloadDw))
				return 0;
			Emit(PkT3(kItNop, payloadDw - 1));
			const uint64_t va = gpuVa + uint64_t{count} * 4;
			dw[count + payloadDw - 1] = 0;
			std::memcpy(dw + count, data, bytes);
			count += payloadDw;
			return va;
		}

		// SET_SH_REG of consecutive compute registers starting at `reg` (a dword offset >= 0x2C00).
		void SetComputeShRegs(uint32_t reg, const uint32_t* values, uint32_t count)
		{
			Emit(PkT3(kItSetShReg, count) | kPkt3ShaderTypeCompute);
			Emit(reg - kShRegBase);
			for (uint32_t i = 0; i < count; ++i)
				Emit(values[i]);
		}

		void SetComputeShReg(uint32_t reg, uint32_t value)
		{
			SetComputeShRegs(reg, &value, 1);
		}

		// SET_SH_REG_INDEX with index 3 (APPLY_KMD_CU_AND_MASK): the CP ANDs the value with the CU
		// mask the KMD gave this queue -- how AMDVLK writes COMPUTE_STATIC_THREAD_MGMT_SE*.
		void SetComputeShRegsCuMasked(uint32_t reg, uint32_t value0, uint32_t value1)
		{
			Emit(PkT3(kItSetShRegIndex, 2) | kPkt3ShaderTypeCompute);
			Emit((3u << 28) | (reg - kShRegBase));
			Emit(value0);
			Emit(value1);
		}

		// ACQUIRE_MEM invalidating the instruction and scalar caches (GCR_CNTL GLI_INV | GLK_INV)
		// over the whole address space, byte-identical to the one AMDVLK puts at the start of every
		// submit: shader code written through the CPU must not be served stale. The graphics ring
		// sets bit 31 of the first dword (PFP); AMDVLK's compute-queue version leaves it 0.
		// vectorCaches: also GLV_INV | GL1_INV (GCR_CNTL bits 8-9), for shaders that read images
		// earlier GPU work wrote.
		void AcquireMemInvalidateShaderCaches(bool computeEngine = false, bool vectorCaches = false)
		{
			Emit(PkT3(kItAcquireMem, 6));
			Emit(computeEngine ? 0u : 0x80000000u);
			Emit(0xFFFFFFFFu);
			Emit(0x00FFFFFFu);
			Emit(0);
			Emit(0);
			Emit(0x0000000Au);
			Emit(vectorCaches ? 0x00000381u : 0x00000081u);
		}

		// Graphics register writes, one packet each (no SHADER_TYPE bit: the graphics pipe).
		void SetContextReg(uint32_t reg, uint32_t value)
		{
			Emit(PkT3(kItSetContextReg, 1));
			Emit(reg - kContextRegBase);
			Emit(value);
		}

		void SetGraphicsShReg(uint32_t reg, uint32_t value)
		{
			Emit(PkT3(kItSetShReg, 1));
			Emit(reg - kShRegBase);
			Emit(value);
		}

		void SetGraphicsShRegCuMasked(uint32_t reg, uint32_t value)
		{
			Emit(PkT3(kItSetShRegIndex, 1));
			Emit((3u << 28) | (reg - kShRegBase));
			Emit(value);
		}

		void SetUconfigReg(uint32_t reg, uint32_t value, uint32_t index = 0)
		{
			Emit(PkT3(index ? kItSetUconfigRegIndex : kItSetUconfigReg, 1));
			Emit((index << 28) | (reg - kUconfigRegBase));
			Emit(value);
		}

		// Resets the context registers to the KMD's golden defaults, as AMDVLK's per-submit preamble.
		void ClearState()
		{
			Emit(PkT3(kItClearState, 0));
			Emit(0);
		}

		void DrawIndexAuto(uint32_t instanceCount, uint32_t vertexCount, uint32_t initiator)
		{
			Emit(PkT3(kItNumInstances, 0));
			Emit(instanceCount);
			Emit(PkT3(kItDrawIndexAuto, 1));
			Emit(vertexCount);
			Emit(initiator);
		}

		// End-of-pipe write of the 64-bit GPU clock (vkCmdWriteTimestamp): RELEASE_MEM of
		// BOTTOM_OF_PIPE_TS, DATA_SEL 3 (GPU clock), through the L2, no interrupt.
		void ReleaseMemTimestamp(uint64_t va)
		{
			Emit(PkT3(kItReleaseMem, 6));
			Emit(0x00000528u);
			Emit(0x60010000u);
			Emit(static_cast<uint32_t>(va));
			Emit(static_cast<uint32_t>(va >> 32));
			Emit(0);
			Emit(0);
			Emit(0);
		}

		// Occlusion queries (PAL Gfx9 OcclusionQueryPool): the DB render backends dump their
		// z-pass counters as {begin, end} pairs of 64-bit values, one pair per RB, 16 bytes apart.
		// PIXEL_PIPE_STAT_CONTROL (event 0x38, index 1) selects OCCLUSION_COUNT_0, a 128-bit stride
		// and the RBs that dump; PIXEL_PIPE_STAT_DUMP (0x39) writes the counters at an address.
		static constexpr uint32_t kOcclusionRbCount = 2; // AMDVLK's instance_enable on this GPU: 0b11
		static constexpr uint32_t kOcclusionSlotBytes = kOcclusionRbCount * 16;

		void PixelPipeStatControl()
		{
			constexpr uint64_t control = (2ull << 9) | (((1ull << kOcclusionRbCount) - 1) << 11); // stride 128 bits, RB mask
			Emit(PkT3(kItEventWrite, 2));
			Emit(0x38u | (1u << 8));
			Emit(static_cast<uint32_t>(control));
			Emit(static_cast<uint32_t>(control >> 32));
		}

		void PixelPipeStatDump(uint64_t va)
		{
			Emit(PkT3(kItEventWrite, 2));
			Emit(0x39u | (1u << 8));
			Emit(static_cast<uint32_t>(va) & ~7u);
			Emit(static_cast<uint32_t>(va >> 32));
		}

		// CONTEXT_CONTROL with state shadowing (PAL Device::GetContextControl, UseStateShadowing):
		// load and shadow the user-config, context and SH registers.
		void ContextControlShadowing()
		{
			Emit(PkT3(kItContextControl, 1));
			Emit(0x81018002u); // update_load_enables | load_cs_sh_regs | load_per_context_state | load_gfx_sh_regs | load_global_uconfig
			Emit(0x81018003u); // the same shadow enables, plus shadow_global_config
		}

		// CONTEXT_CONTROL as PAL writes it without state shadowing (Device::GetContextControl):
		// load per-context state and SH registers, every shadow enable cleared. Without it the CP
		// keeps whatever shadowing setup the previous user of the graphics pipe left -- shadow
		// writes of our registers then land at that process's addresses (GPU write faults at VAs
		// we never mapped).
		void ContextControlNoShadowing()
		{
			Emit(PkT3(kItContextControl, 1));
			Emit(0x81010002u); // update_load_enables | load_cs_sh_regs | load_gfx_sh_regs | load_per_context_state
			Emit(0x80000000u); // update_shadow_enables, all shadowing off
		}

		// Indexed draw as AMDVLK emits it (wddm-dump [draw][re][indexed]): MAX_SIZE = indices
		// readable from indexVa (the bound buffer's end), INDEX_BASE = buffer + offset + firstIndex
		// * size, the count, DRAW_INITIATOR 0 (indices fetched by DMA). VGT_INDEX_TYPE is set apart.
		void DrawIndex2(uint32_t instanceCount, uint32_t maxIndices, uint64_t indexVa, uint32_t indexCount)
		{
			Emit(PkT3(kItNumInstances, 0));
			Emit(instanceCount);
			Emit(PkT3(kItDrawIndex2, 4));
			Emit(maxIndices);
			Emit(static_cast<uint32_t>(indexVa));
			Emit(static_cast<uint32_t>(indexVa >> 32));
			Emit(indexCount);
			Emit(0);
		}

		// AMDVLK's barrier after a render pass (wddm-dump [draw][re][minimal]): end-of-pipe cache
		// action, then the matching ACQUIRE_MEM, so later CP DMA reads see the color target. Also
		// the PWS end-of-pipe stall of the per-submit preamble (PAL BuildWaitEopPws).
		void PostDrawBarrier()
		{
			Emit(PkT3(kItReleaseMem, 6));
			Emit(0x80000528u);
			Emit(0x00010000u);
			for (int i = 0; i < 5; ++i)
				Emit(0);
			Emit(PkT3(kItAcquireMem, 6));
			Emit(0x00022800u);
			Emit(0xFFFFFFFFu);
			Emit(0x01FFFFFFu);
			Emit(0);
			Emit(0);
			Emit(0x80000000u);
			Emit(0);
		}

		void DispatchDirect(uint32_t x, uint32_t y, uint32_t z, uint32_t initiator)
		{
			Emit(PkT3(kItDispatchDirect, 3) | kPkt3ShaderTypeCompute);
			Emit(x);
			Emit(y);
			Emit(z);
			Emit(initiator);
		}

		void EventWrite(uint32_t eventDword)
		{
			Emit(PkT3(kItEventWrite, 0));
			Emit(eventDword);
		}

		// WAIT_REG_MEM on the PFP until the dword at gpuVa equals reference.
		void WaitMemEqual(uint64_t gpuVa, uint32_t reference)
		{
			Emit(PkT3(kItWaitRegMem, 5));
			Emit(0x00000113u); // function == , memory space, PFP
			Emit(static_cast<uint32_t>(gpuVa));
			Emit(static_cast<uint32_t>(gpuVa >> 32));
			Emit(reference);
			Emit(0xFFFFFFFFu);
			Emit(0x0000000Au); // poll interval
		}

		// LOAD_UCONFIG_REG / LOAD_CONTEXT_REG / LOAD_SH_REG: reload the registers of `ranges` from
		// the register-space mirror at spaceVa.
		void LoadRegisters(uint32_t opcode, uint64_t spaceVa, std::span<const ShadowRegisterRange> ranges, bool compute = false)
		{
			Emit(PkT3(opcode, 2 * static_cast<uint32_t>(ranges.size()) + 1) | (compute ? kPkt3ShaderTypeCompute : 0u));
			Emit(static_cast<uint32_t>(spaceVa));
			Emit(static_cast<uint32_t>(spaceVa >> 32));
			for (const ShadowRegisterRange& range : ranges)
			{
				Emit(range.offset);
				Emit(range.count);
			}
		}

		// The per-submit preamble of a graphics queue with state shadowing, as AMDVLK's (wddm-dump
		// [submit-records][re], checked by [shadow][vkd-pm4]): wait for the previous submit's
		// postamble (semaphore back to 0), invalidate the shader caches, drain the pipe before the
		// ring registers are reloaded, CONTEXT_CONTROL, CLEAR_STATE, reload every shadowed register.
		void GfxSubmitPreamble(uint64_t shadowVa, uint64_t semaphoreVa)
		{
			WaitMemEqual(semaphoreVa, 0);
			AcquireMemInvalidateShaderCaches();
			EventWrite(kEventVsPartialFlush);
			EventWrite(kEventVgtFlush);
			PostDrawBarrier();
			ContextControlShadowing();
			ClearState();
			LoadRegisters(kItLoadUconfigReg, shadowVa + kShadowUserConfigOffset, kShadowUserConfigRanges);
			LoadRegisters(kItLoadContextReg, shadowVa + kShadowContextOffset, kShadowContextRanges);
			LoadRegisters(kItLoadShReg, shadowVa + kShadowShOffset, kShadowGfxShRanges);
			LoadRegisters(kItLoadShReg, shadowVa + kShadowShOffset, kShadowCsShRanges, true);
		}

		// The graphics queue's first submit: the preamble, a reload of the zeroed user-config and SH
		// shadow, then a write of every context register, which the CP shadows -- from then on the
		// shadow memory holds a value for each register a resumed preemption reloads.
		void GfxShadowInit(uint64_t shadowVa, uint64_t semaphoreVa)
		{
			GfxSubmitPreamble(shadowVa, semaphoreVa);
			LoadRegisters(kItLoadUconfigReg, shadowVa + kShadowUserConfigOffset, kShadowUserConfigRanges);
			LoadRegisters(kItLoadShReg, shadowVa + kShadowShOffset, kShadowGfxShRanges);
			LoadRegisters(kItLoadShReg, shadowVa + kShadowShOffset, kShadowCsShRanges, true);
			for (uint32_t value : kShadowContextInitPm4)
				Emit(value);
		}

		void WriteData(uint64_t gpuVa, uint32_t value)
		{
			WriteDataHeader(gpuVa, 1);
			Emit(value);
		}

		// The 96-byte postamble AMDVLK passes as SubmitPrivateData record 2 on every submit
		// (wddm-dump [submit-records][re], byte-identical across submits): WRITE_DATA sem=1, then
		// RELEASE_MEM(CACHE_FLUSH_AND_INV_TS, EOP, int_sel=3 -> interrupt) sem=0, NOP padding.
		// The EOP interrupt is what completes the submit for the KMD.
		void SubmitPostamble(uint64_t semaphoreVa)
		{
			WriteData(semaphoreVa, 1);
			Emit(PkT3(kItReleaseMem, 6));
			Emit(0x0030E514u);
			Emit(0x23010000u);
			Emit(static_cast<uint32_t>(semaphoreVa));
			Emit(static_cast<uint32_t>(semaphoreVa >> 32));
			Emit(0);
			Emit(0);
			Emit(0);
			NopBlock(7);
			NopBlock(4);
		}
	};
} // namespace vkd::amd

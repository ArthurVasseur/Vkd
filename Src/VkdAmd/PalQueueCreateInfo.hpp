#pragma once

#include <cstddef>
#include <cstdint>

namespace vkd::amd
{
	// PAL QueueType as filled by FUN_18025d040 (amdvlk64 0x18025d040).
	// Table at engine+(familyIndex+0x100)*0x2c.
	// Open-source pal.h has Timer=3; the ICD blob skips 3 and uses 4/5 for
	// Timer / VCN (see PAL_WddmInterface_CreateContextVirtual_Full param_2+8).
	enum class PalQueueType : uint32_t
	{
		Universal = 0,
		Compute = 1,
		Dma = 2,
		Timer = 4,
		Special = 5,
	};

	// PAL EngineType (pal.h). FUN_18025d040 writes this at blob[1].
	enum class PalEngineType : uint32_t
	{
		Universal = 0,
		Compute = 1,
		Dma = 2,
		Timer = 3,
	};

	// PAL SubmitOptMode (palQueue.h). FUN_18025d040 never writes blob[3] → Default.
	enum class PalSubmitOptMode : uint32_t
	{
		Default = 0,
		Disabled = 1,
		MinKernelSubmits = 2,
		MinGpuCmdOverhead = 3,
	};

	// PAL QueuePriority (pal.h). FUN_180261810 maps VkQueueGlobalPriority → 0..4.
	enum class PalQueuePriority : uint32_t
	{
		Normal = 0,
		Idle = 1,
		Medium = 2,
		High = 3,
		Realtime = 4,
	};

	// PAL QueueCreateInfo flags (palQueue.h anonymous bitfield).
	// ICD (FUN_18025d040):
	//   bit2 tmzOnly                    = param_8
	//   bit4 dispatchTunneling          = high-prio compute (Vk 0x200/0x400)
	//   bit5 forceWaitIdleOnRingResize  = always set
	union PalQueueCreateFlags
	{
		uint32_t u32All;
		struct
		{
			uint32_t placeholder1 : 1;
			uint32_t windowedPriorBlit : 1;
			uint32_t tmzOnly : 1;
			uint32_t placeholder2 : 1;
			uint32_t dispatchTunneling : 1;
			uint32_t forceWaitIdleOnRingResize : 1;
			uint32_t placeholder3 : 1;
			uint32_t reserved : 25;
		} bits;
	};

	// 40-byte blob passed to PAL vtable+0x160 (FUN_18025d7e0 local_898[10]).
	// Matches palQueue.h QueueCreateInfo with PAL_CLIENT_INTERFACE_MAJOR_VERSION >= 914
	// (persistentCeRam fields stripped; aqlPacketList 8-byte aligned).
	struct PalQueueCreateInfo
	{
		PalQueueType queueType = PalQueueType::Universal; // +0x00 blob[0]
		PalEngineType engineType = PalEngineType::Universal; // +0x04 blob[1]
		uint32_t engineIndex = 0; // +0x08 blob[2]
		PalSubmitOptMode submitOptMode = PalSubmitOptMode::Default; // +0x0C blob[3]
		PalQueuePriority priority = PalQueuePriority::Normal; // +0x10 blob[4]
		PalQueueCreateFlags flags = {}; // +0x14 blob[5]
		uint32_t numReservedCu = 0; // +0x18 blob[6]
		uintptr_t aqlPacketList = 0; // +0x20 blob[8..9], HIP only
	};
	static_assert(sizeof(PalQueueCreateInfo) == 40);
	static_assert(offsetof(PalQueueCreateInfo, queueType) == 0x00);
	static_assert(offsetof(PalQueueCreateInfo, engineType) == 0x04);
	static_assert(offsetof(PalQueueCreateInfo, engineIndex) == 0x08);
	static_assert(offsetof(PalQueueCreateInfo, submitOptMode) == 0x0C);
	static_assert(offsetof(PalQueueCreateInfo, priority) == 0x10);
	static_assert(offsetof(PalQueueCreateInfo, flags) == 0x14);
	static_assert(offsetof(PalQueueCreateInfo, numReservedCu) == 0x18);
	static_assert(offsetof(PalQueueCreateInfo, aqlPacketList) == 0x20);

	inline PalQueueType QueueFamilyToPalType(uint32_t queueFamilyIndex)
	{
		if (queueFamilyIndex == 1)
			return PalQueueType::Compute;
		if (queueFamilyIndex == 2)
			return PalQueueType::Dma;
		return PalQueueType::Universal;
	}

	inline PalEngineType QueueFamilyToPalEngine(uint32_t queueFamilyIndex)
	{
		if (queueFamilyIndex == 1)
			return PalEngineType::Compute;
		if (queueFamilyIndex == 2)
			return PalEngineType::Dma;
		return PalEngineType::Universal;
	}

	// FUN_180261810: VkQueueGlobalPriority (0x80/0x100/0x200/0x400) → PalQueuePriority
	// using capability bits at engineProperties+4 (QueuePrioritySupport).
	inline PalQueuePriority MapQueueGlobalPriority(int32_t vkPriority, uint32_t capBits)
	{
		const bool b0 = (capBits & 1) != 0;
		const bool b1 = (capBits & 2) != 0;
		const bool b2 = (capBits & 4) != 0;
		const bool b3 = (capBits & 8) != 0;
		const bool b4 = (capBits & 16) != 0;

		if (vkPriority == 0x80)
		{
			if (b1)
				return PalQueuePriority::Idle;
			if (b0)
				return PalQueuePriority::Normal;
			if (b2)
				return PalQueuePriority::Medium;
			if (!b3)
				return b4 ? PalQueuePriority::Realtime : PalQueuePriority::Normal;
			return PalQueuePriority::High;
		}
		if (vkPriority == 0x200)
		{
			if (b3)
				return PalQueuePriority::High;
			if (b2)
				return PalQueuePriority::Medium;
			if (b0)
				return PalQueuePriority::Normal;
			if (b1)
				return PalQueuePriority::Idle;
			return b4 ? PalQueuePriority::Realtime : PalQueuePriority::Normal;
		}
		if (vkPriority == 0x400)
		{
			if (b4)
				return PalQueuePriority::Realtime;
			if (b3)
				return PalQueuePriority::High;
			if (b2)
				return PalQueuePriority::Medium;
			if (b0)
				return PalQueuePriority::Normal;
			return b1 ? PalQueuePriority::Idle : PalQueuePriority::Normal;
		}
		if (b0)
			return PalQueuePriority::Normal;
		if (b2)
			return PalQueuePriority::Medium;
		if (b3)
			return PalQueuePriority::High;
		if (b1)
			return PalQueuePriority::Idle;
		return b4 ? PalQueuePriority::Realtime : PalQueuePriority::Normal;
	}

	// Vkd-side fill (no PAL device object). Matches FUN_18025d040 for the
	// 3 Vulkan families we expose: Universal / Compute / DMA.
	// vkGlobalPriority defaults to MEDIUM (0x100). capBits 0x1F = all 5 PAL slots.
	inline PalQueueCreateInfo FillPalQueueCreateInfo(uint32_t queueFamilyIndex, uint32_t queueIndex,
													 uint32_t extraFlagBit = 0,
													 int32_t vkGlobalPriority = 0x100)
	{
		PalQueueCreateInfo info{};
		info.queueType = QueueFamilyToPalType(queueFamilyIndex);
		info.engineType = QueueFamilyToPalEngine(queueFamilyIndex);
		info.engineIndex = queueIndex;
		info.flags.u32All = ((extraFlagBit & 1u) << 2) | 0x20;
		info.priority = MapQueueGlobalPriority(vkGlobalPriority, 0x1F);
		if (info.queueType == PalQueueType::Compute &&
			(vkGlobalPriority == 0x200 || vkGlobalPriority == 0x400))
		{
			info.flags.u32All |= 0x10;
		}
		return info;
	}
} // namespace vkd::amd

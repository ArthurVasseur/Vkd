#pragma once

#include <cstddef>
#include <cstdint>

namespace vkd::amd
{
	// AMD WDDM context private data — 424 bytes = 0x1A8.
	//
	// Captured via wddm-dump [context][re][privdata] on STRIX_HALO / gfx1151.
	// Filled by PAL_WddmInterface_CreateContextVirtual_Full (amdvlk64 0x18053db30):
	//   memset 0x1A8
	//   FUN_1803bf7c0(device, &hwContextFlags, engineType, engineIndex, windowedPriorBlit)
	//   outerSize = 0x40
	//   if device+0xd67c bit7 (STRIX_HALO: always):
	//       hwContextFlags |= 0x00100000
	//       innerSize = 0x168
	//       queueExtra150 = Queue.aqlPacketList (0 for Vulkan)
	//   hwContextFlags bit23 from platform flags / queue obj+0x28==2
	//   extraFlags014 bit7 if node-table index == 0x31 (VCN)
	//   field038 bit21 from device+0x21c6
	//
	// Captured blob for the 7 Vulkan contexts (nodes 0,1,2,1,2,7,7) is identical:
	//   +0x000 = 0x40, +0x004 = 0x00100000, +0x040 = 0x168, rest 0.
	struct ContextPrivateData
	{
		uint32_t outerSize = 0x40; // +0x000 self-size of outer header
		uint32_t hwContextFlags = 0x00100000; // +0x004 bit20 = HW queue
		uint8_t reserved008[0x0C]{}; // +0x008 .. +0x013
		uint32_t extraFlags014 = 0; // +0x014 bit7 if VCN node 0x31
		uint8_t reserved018[0x04]{}; // +0x018
		uint32_t field01c = 0; // +0x01C
		uint64_t field020 = 0; // +0x020
		uint8_t reserved028[0x08]{}; // +0x028
		uint32_t field030 = 0; // +0x030 cleared on VCN special
		uint8_t reserved034[0x04]{}; // +0x034
		uint32_t field038 = 0; // +0x038 bit21 from device+0x21c6
		uint8_t reserved03c[0x04]{}; // +0x03C
		uint32_t innerSize = 0x168; // +0x040 self-size of inner struct
		uint8_t inner044[0x10C]{}; // +0x044 .. +0x14F
		uint64_t queueExtra150 = 0; // +0x150 Queue.aqlPacketList
		uint8_t inner158[0x50]{}; // +0x158 .. +0x1A7
	};
	static_assert(sizeof(ContextPrivateData) == 0x1A8);
	static_assert(offsetof(ContextPrivateData, outerSize) == 0x000);
	static_assert(offsetof(ContextPrivateData, hwContextFlags) == 0x004);
	static_assert(offsetof(ContextPrivateData, extraFlags014) == 0x014);
	static_assert(offsetof(ContextPrivateData, field01c) == 0x01C);
	static_assert(offsetof(ContextPrivateData, field020) == 0x020);
	static_assert(offsetof(ContextPrivateData, field030) == 0x030);
	static_assert(offsetof(ContextPrivateData, field038) == 0x038);
	static_assert(offsetof(ContextPrivateData, innerSize) == 0x040);
	static_assert(offsetof(ContextPrivateData, queueExtra150) == 0x150);
} // namespace vkd::amd

#pragma once

#include <cstdint>
#include <cstring>

namespace vkd::amd
{
	// D3DKMTCreateHwQueue private data — 64 bytes, captured from AMDVLK dump.json.
	// +0x00 = 0x40, +0x10 = 0x1000, +0x14 and +0x1C = engine word.
	struct HwQueuePrivateData
	{
		uint32_t size = 0x40;
		uint32_t reserved04 = 0;
		uint64_t ringVa = 0;
		uint32_t flags = 0x1000;
		uint32_t engineWord = 0x4100;
		uint32_t reserved18 = 0;
		uint32_t engineWord2 = 0x4100;
		uint8_t tail[32]{};
	};
	static_assert(sizeof(HwQueuePrivateData) == 64);

	inline uint32_t HwQueueEngineWord(uint32_t nodeOrdinal)
	{
		// Live AMDVLK CreateHwQueue on STRIX_HALO:
		//   0x4100 GFX (node 0, PalQueueType::Universal)
		//   0x4000 ACE (node 1, PalQueueType::Compute)
		//   0x4002 SDMA (node 2, PalQueueType::Dma)
		//   0x5680 / 0x5682 extra queues (PalQueueType::Timer=4 / Special=5, VCN)
		switch (nodeOrdinal)
		{
			case 1:
				return 0x4000;
			case 2:
				return 0x4002;
			case 3:
			case 7:
				return 0x5680;
			case 4:
				return 0x5682;
			default:
				return 0x4100;
		}
	}

	inline HwQueuePrivateData MakeHwQueuePrivateData(uint32_t nodeOrdinal)
	{
		HwQueuePrivateData data{};
		const uint32_t word = HwQueueEngineWord(nodeOrdinal);
		data.engineWord = word;
		data.engineWord2 = word;
		return data;
	}
} // namespace vkd::amd

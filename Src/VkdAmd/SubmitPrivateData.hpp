#pragma once

#include <cstdint>

namespace vkd::amd
{
	// D3DKMTSubmitCommandToHwQueue private data: the record list amdkmdag.sys parses in
	// nsUSR::CS_Context::ValidateVmOSCommandBuffer (RADV's wddm2 winsys has the same shape,
	// struct submit_pdd_gfx_ib). One 64-byte record per IB the GPU executes for the submit;
	// AMDVLK always sends 3:
	//   rec0 (flags 0xC)   : the preamble (= D3DKMT CommandBuffer/CommandLength); AMDVLK's sets up
	//                        state shadowing, Vkd's turns it off (Pm4Stream::ContextControlNoShadowing)
	//   rec1 (flags 0)     : the command buffer, the main IB
	//   rec2 (flags 0x204) : the 96-byte postamble (Pm4Stream::SubmitPostamble), whose EOP
	//                        interrupt is what completes the submit for the KMD
	// RADV's wddm2 winsys uses the same flags for its preamble / main / postamble IBs.
	// All three must be real, resident, GPU-readable PM4. The struct reproduces AMDVLK's captured
	// 200-byte blob byte for byte (wddm-dump [submit-records][re]); the history -- a missing
	// record opcode, then records pointing at the command buffer itself -- is in
	// Amd/docs/amd_wddm_private_driver_data.md.
	struct SubmitRecord
	{
		uint32_t type = 0; // UDMABUFFER_INFO; the KMD accepts opcodes {0, 1, 3, 4, 5, 6}
		uint32_t size = 64;
		uint32_t length = 0; // IB bytes
		uint32_t flags = 0;
		uint32_t reserved = 0;
		uint32_t ipType = 1;
		uint32_t gpuVaLo = 0;
		uint32_t gpuVaHi = 0;
		uint32_t padding[8] = {};
	};
	static_assert(sizeof(SubmitRecord) == 64);

	struct SubmitPrivateData
	{
		uint32_t clientId = 9;
		uint32_t recordCount = 3;
		SubmitRecord records[3];
	};
	static_assert(sizeof(SubmitPrivateData) == 200);

	inline constexpr uint32_t kSubmitPostambleBytes = 0x60;

	struct SubmitIb
	{
		uint64_t gpuVa = 0;
		uint32_t length = 0;
	};

	inline SubmitPrivateData MakeSubmitPrivateData(const SubmitIb& rec0, const SubmitIb& rec1, const SubmitIb& rec2)
	{
		constexpr uint32_t kRecordFlags[3] = {0xC, 0x0, 0x204};
		const SubmitIb ibs[3] = {rec0, rec1, rec2};
		SubmitPrivateData data;
		for (int i = 0; i < 3; ++i)
		{
			data.records[i].length = ibs[i].length;
			data.records[i].flags = kRecordFlags[i];
			data.records[i].gpuVaLo = static_cast<uint32_t>(ibs[i].gpuVa);
			data.records[i].gpuVaHi = static_cast<uint32_t>(ibs[i].gpuVa >> 32);
		}
		return data;
	}
} // namespace vkd::amd

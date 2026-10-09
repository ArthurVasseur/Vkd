#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

namespace vkd::amd
{
	// GFX11 machine code for known SPIR-V compute modules, until Vkd compiles SPIR-V itself. Each
	// entry is what AMDVLK produced for that exact module, with the shader registers it programs
	// alongside (wddm-dump [dispatch][re]; full capture in
	// Amd/Src/WddmDump/Captures/ComputeDispatchCapture.hpp). A module that isn't listed gets a
	// pipeline whose dispatches are skipped.
	// A descriptor the shader reads from a descriptor table: set/binding, and its byte offset in the
	// table AMDVLK lays out for that set (16-byte buffer descriptors).
	struct PrecompiledDescriptor
	{
		uint32_t set;
		uint32_t binding;
		VkDescriptorType type;
		uint32_t offset;
		uint32_t count = 1;
	};

	inline constexpr uint32_t kMaxPrecompiledSets = 4;

	struct PrecompiledComputeShader
	{
		uint64_t spirvHash; // FNV-1a 64 of the SPIR-V bytes
		std::span<const uint32_t> code;
		uint32_t pgmRsrc1;
		uint32_t pgmRsrc2;
		uint32_t pgmRsrc3;
		uint32_t numThreadX;
		uint32_t numThreadY;
		uint32_t numThreadZ;
		uint32_t dispatchInitiator;
		uint32_t pushConstantUserData; // COMPUTE_USER_DATA_n holding push-constant dword 0
		uint32_t pushConstantDwords;
		// COMPUTE_USER_DATA_n holding each set's 32-bit table pointer (0 = set not used: user data 0
		// is PAL's global table).
		uint32_t setTableUserData[kMaxPrecompiledSets] = {};
		std::span<const PrecompiledDescriptor> descriptors = {};
	};

	inline uint64_t HashSpirv(std::span<const uint32_t> words)
	{
		uint64_t hash = 0xCBF29CE484222325ull;
		for (const uint32_t word : words)
		{
			for (int i = 0; i < 4; ++i)
			{
				hash ^= (word >> (8 * i)) & 0xFFu;
				hash *= 0x100000001B3ull;
			}
		}
		return hash;
	}

	// layout(local_size_x = 64); push constants { uint64 dst; uint mul; uint count; };
	// if (i < count) dst[i] = i * mul + 0xC0DE0000 -- glslc --target-env=vulkan1.3 -O
	// (Amd/Src/WddmDump/Tests/DispatchRE.cpp). Wave64; s0 = global table (unused), s1..s4 = push
	// constants, s5.. = workgroup ids. Ends in s_endpgm, then s_code_end padding for prefetch.
	inline constexpr uint32_t kFillBufferAddressCode[] = {
		0x360000ff,
		0x000003ff,
		0xbf870091,
		0xd6460000,
		0x04010c05,
		0x7c980004,
		0xbe84216a,
		0xbfa5000e,
		0x3402009f,
		0xd6fe7c03,
		0x03fc0700,
		0xc0de0000,
		0xbf870092,
		0xd73c0001,
		0x00020082,
		0xd7006a00,
		0x00020201,
		0xbf870001,
		0xd5207c01,
		0x01aa0402,
		0xdc6a0000,
		0x007c0300,
		0xbfb00000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
	};

	// layout(local_size_x = 64); layout(set = 0, binding = 0) buffer { uint data[]; };
	// push constants { uint mul; uint count; }; if (i < count) data[i] = i * mul + 0xBEEF0000
	// (wddm-dump [dispatch][re][descriptor]). s0 = global table, s1..s2 = push constants, s3 = set 0
	// table pointer, completed by s_getpc_b64's high half; s_load_b128 then buffer_store_b32.
	inline constexpr uint32_t kFillDescriptorCode[] = {
		0x360000ff,
		0x000003ff,
		0xbe860003,
		0xbf870091,
		0xd6460000,
		0x04010c04,
		0x7c980002,
		0xbe82216a,
		0xbfa5000b,
		0xbe824700,
		0xd6fe7c01,
		0x03fc0300,
		0xbeef0000,
		0xbe870003,
		0x30000082,
		0xf4080103,
		0xf8000000,
		0xbf89fc07,
		0xe0680000,
		0x80410100,
		0xbfb00000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
		0xbf9f0000,
	};

	inline constexpr PrecompiledDescriptor kFillDescriptorBindings[] = {
		{0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0},
	};

	inline constexpr PrecompiledComputeShader kPrecompiledComputeShaders[] = {
		{
			.spirvHash = 0x9FAFD8DF4B05A892ull,
			.code = kFillBufferAddressCode,
			.pgmRsrc1 = 0x402C0041,
			.pgmRsrc2 = 0x0000078A, // 5 user SGPRs, TGID_X/Y/Z, TG_SIZE
			.pgmRsrc3 = 0x80000000,
			.numThreadX = 64,
			.numThreadY = 1,
			.numThreadZ = 1,
			.dispatchInitiator = 0x00002045,
			.pushConstantUserData = 1,
			.pushConstantDwords = 4,
		},
		{
			.spirvHash = 0xF9312E79587A6654ull,
			.code = kFillDescriptorCode,
			.pgmRsrc1 = 0x402C0040,
			.pgmRsrc2 = 0x00000788, // 4 user SGPRs, TGID_X/Y/Z, TG_SIZE
			.pgmRsrc3 = 0x80000000,
			.numThreadX = 64,
			.numThreadY = 1,
			.numThreadZ = 1,
			.dispatchInitiator = 0x00002045,
			.pushConstantUserData = 1,
			.pushConstantDwords = 2,
			.setTableUserData = {3, 0, 0, 0},
			.descriptors = kFillDescriptorBindings,
		},
	};

	inline const PrecompiledComputeShader* FindPrecompiledComputeShader(std::span<const uint32_t> spirv)
	{
		const uint64_t hash = HashSpirv(spirv);
		for (const PrecompiledComputeShader& shader : kPrecompiledComputeShaders)
		{
			if (shader.spirvHash == hash)
				return &shader;
		}
		return nullptr;
	}
} // namespace vkd::amd

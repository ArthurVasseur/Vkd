#pragma once

#include <array>
#include <cstdint>

// amd-addr: layout of AMD tiled surfaces -- how much memory the hardware addresses for a swizzle mode,
// and where each mip level sits. TA, DB and CB compute every texel address from the base and the
// dimensions themselves; this only has to agree with them on the sizes. Pure computation (no Vulkan,
// no driver state). The rules are AddrLib's (MIT, PAL src/core/imported/addrlib), reimplemented for
// the cases Vkd uses and checked against it: amd-addr-tests (reference values generated from
// AddrLib) and Amd's wddm-dump ([addrlib][vkd-tiling], AddrLib itself on a large grid).
namespace amdaddr::gfx11
{
	// Hardware swizzle mode numbers (DB_Z_INFO.SW_MODE, T# SW_MODE).
	inline constexpr uint32_t kSwModeLinear = 0;
	inline constexpr uint32_t kSwMode64KbZX = 24; // AMDVLK's choice for depth/stencil attachments

	inline constexpr uint32_t kMaxMipLevels = 16;
	inline constexpr uint64_t kBlock64Kb = 0x10000;

	struct MipPlacement
	{
		uint64_t offset; // from the start of the array layer
		uint32_t pitch; // elements, block aligned (outside the tail)
		uint32_t height;
		bool inTail;
	};

	struct SurfaceLayout
	{
		uint32_t swizzleMode = kSwModeLinear;
		uint32_t bytesPerElement = 0;
		uint32_t blockWidth = 0; // elements in one 64KB block
		uint32_t blockHeight = 0;
		uint32_t pitch = 0; // mip 0, elements
		uint32_t height = 0;
		uint32_t mipLevels = 0;
		uint32_t firstMipInTail = 0; // == mipLevels when no level is in the tail
		uint64_t sliceSize = 0; // one array layer, whole mip chain
		uint64_t size = 0; // every array layer
		std::array<MipPlacement, kMaxMipLevels> mips{};
	};

	// SW_64KB_Z_X, thin 2D, one sample (Gfx11Lib::ComputeSurfaceInfoMacroTiled). bytesPerElement is
	// 1, 2, 4, 8 or 16; mipLevels 1..kMaxMipLevels.
	SurfaceLayout Compute64KbZLayout(uint32_t bytesPerElement, uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t arrayLayers);
} // namespace amdaddr::gfx11

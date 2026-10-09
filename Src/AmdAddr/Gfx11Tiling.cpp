#include "AmdAddr/Gfx11Tiling.hpp"

#include <algorithm>

namespace amdaddr::gfx11
{
	namespace
	{
		constexpr uint32_t Log2(uint32_t v)
		{
			uint32_t l = 0;
			while (v > 1)
			{
				v >>= 1;
				++l;
			}
			return l;
		}

		constexpr uint32_t AlignPow2(uint32_t v, uint32_t a)
		{
			return (v + a - 1) & ~(a - 1);
		}

		// Mip dimensions round up (AddrLib's ShiftCeil), unlike Vulkan's: the hardware lays out mip i
		// as if it were ceil(size / 2^i).
		constexpr uint32_t MipDim(uint32_t mip0, uint32_t level)
		{
			return (std::max(mip0, 1u) + (1u << level) - 1) >> level;
		}

		// Dimensions of a 256-byte micro block, by log2(bytes per element).
		constexpr uint32_t kBlock256Width[] = {16, 16, 8, 8, 4};
		constexpr uint32_t kBlock256Height[] = {16, 8, 8, 4, 4};
	} // namespace

	SurfaceLayout Compute64KbZLayout(uint32_t bytesPerElement, uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t arrayLayers)
	{
		SurfaceLayout layout;
		layout.swizzleMode = kSwMode64KbZX;
		layout.bytesPerElement = bytesPerElement;
		layout.mipLevels = mipLevels;

		const uint32_t log2Elem = Log2(bytesPerElement);
		const uint32_t log2Elems = 16 - log2Elem;
		const uint32_t log2Width = (log2Elems + 1) / 2; // thin, one sample: width >= height
		layout.blockWidth = 1u << log2Width;
		layout.blockHeight = 1u << (log2Elems - log2Width);
		layout.pitch = AlignPow2(width, layout.blockWidth);
		layout.height = AlignPow2(height, layout.blockHeight);

		if (mipLevels <= 1)
		{
			layout.firstMipInTail = mipLevels;
			layout.sliceSize = uint64_t{layout.pitch} * layout.height * bytesPerElement;
			layout.mips[0] = {0, layout.pitch, layout.height, false};
			layout.size = layout.sliceSize * arrayLayers;
			return layout;
		}

		// The tail -- one block holding every level small enough -- takes levels up to half a block
		// wide. Z16 and stencil enter it with the Z32 dimensions (so HTILE stays consistent).
		uint32_t tailWidth = layout.blockWidth >> 1;
		uint32_t tailHeight = layout.blockHeight;
		if (log2Elem <= 1)
		{
			tailWidth /= kBlock256Width[log2Elem] / kBlock256Width[2];
			tailHeight /= kBlock256Height[log2Elem] / kBlock256Height[2];
		}
		constexpr uint32_t kMaxMipsInTail = 16 - 4; // 64KB thin block

		std::array<uint64_t, kMaxMipLevels> levelSize{};
		layout.firstMipInTail = mipLevels;
		uint64_t chain = 0;
		for (uint32_t level = 0; level < mipLevels; ++level)
		{
			const uint32_t w = MipDim(width, level);
			const uint32_t h = MipDim(height, level);
			if (w <= tailWidth && h <= tailHeight && mipLevels - level <= kMaxMipsInTail)
			{
				layout.firstMipInTail = level;
				chain += kBlock64Kb;
				break;
			}
			const uint32_t pitch = AlignPow2(w, layout.blockWidth);
			const uint32_t rows = AlignPow2(h, layout.blockHeight);
			levelSize[level] = uint64_t{pitch} * rows * bytesPerElement;
			layout.mips[level] = {0, pitch, rows, false};
			chain += levelSize[level];
		}

		// The tail block comes first, then the levels from the smallest: level 0 is last.
		uint64_t offset = layout.firstMipInTail != mipLevels ? kBlock64Kb : 0;
		for (uint32_t level = layout.firstMipInTail; level-- > 0;)
		{
			layout.mips[level].offset = offset;
			offset += levelSize[level];
		}
		for (uint32_t level = layout.firstMipInTail; level < mipLevels; ++level)
		{
			const uint32_t m = kMaxMipsInTail - 1 - (level - layout.firstMipInTail);
			const uint32_t mipOffset = m > 6 ? (16u << m) : (m << 8);
			layout.mips[level] = {mipOffset, tailWidth, tailHeight, true};
		}

		layout.sliceSize = chain;
		layout.size = chain * arrayLayers;
		return layout;
	}
} // namespace amdaddr::gfx11

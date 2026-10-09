#include <catch2/catch_test_macros.hpp>

#include "AmdAddr/Gfx11Tiling.hpp"
#include "AmdAddr/Tests/Gfx11TilingReference.hpp"

using namespace amdaddr::gfx11;

TEST_CASE("SW_64KB_Z_X layouts match AddrLib", "[amd-addr][gfx11]")
{
	for (const reference::Layout& ref : reference::kLayouts)
	{
		const SurfaceLayout layout = Compute64KbZLayout(ref.bytesPerElement, ref.width, ref.height, ref.mipLevels, ref.arrayLayers);
		INFO(ref.bytesPerElement << " bytes, " << ref.width << "x" << ref.height << ", " << ref.mipLevels << " mips, " << ref.arrayLayers << " layers");
		CHECK(layout.size == ref.size);
		CHECK(layout.sliceSize == ref.sliceSize);
		CHECK(layout.pitch == ref.pitch);
		CHECK(layout.height == ref.alignedHeight);
		CHECK(layout.firstMipInTail == ref.firstMipInTail);
		for (uint32_t mip = 0; mip < ref.mipLevels; ++mip)
			CHECK(layout.mips[mip].offset == ref.mipOffsets[mip]);
	}
}

TEST_CASE("SW_64KB_Z_X block dimensions", "[amd-addr][gfx11]")
{
	// 64KB of 1/2/4-byte elements: 256x256, 256x128, 128x128 (AMDVLK's depth/stencil attachments).
	CHECK(Compute64KbZLayout(1, 1, 1, 1, 1).blockWidth == 256);
	CHECK(Compute64KbZLayout(1, 1, 1, 1, 1).blockHeight == 256);
	CHECK(Compute64KbZLayout(2, 1, 1, 1, 1).blockWidth == 256);
	CHECK(Compute64KbZLayout(2, 1, 1, 1, 1).blockHeight == 128);
	CHECK(Compute64KbZLayout(4, 1, 1, 1, 1).blockWidth == 128);
	CHECK(Compute64KbZLayout(4, 1, 1, 1, 1).blockHeight == 128);
}

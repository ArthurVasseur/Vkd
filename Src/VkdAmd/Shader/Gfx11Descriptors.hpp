#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan.h>

// GFX11 resource descriptors as shaders read them from descriptor tables: buffer (V#, 4 dwords),
// image (T#, 8 dwords), sampler (S#, 4 dwords). Field layouts are PAL's sq_buf_rsrc_t, sq_img_rsrc_t
// (its rtIp2Plus = GFX11 variant) and sq_img_samp_t (deps/pal .../gfx9/chip/gfx10_sq_ko_reg.h);
// enum values are PAL's gfx9_plus_merged_enum.h. Fields Vulkan doesn't drive take the values AMDVLK
// writes (wddm-dump [dispatch][re][descriptor] and [draw][re][testapp]).
namespace vkd::amd::gfx11
{
	// SQ_SEL_*: what a descriptor channel reads.
	inline constexpr uint32_t kSel0 = 0;
	inline constexpr uint32_t kSel1 = 1;
	inline constexpr uint32_t kSelX = 4;
	inline constexpr uint32_t kSelY = 5;
	inline constexpr uint32_t kSelZ = 6;
	inline constexpr uint32_t kSelW = 7;

	// Storage/uniform buffer: base address, NUM_RECORDS = range in bytes, and the dword AMDVLK uses
	// for raw buffers (dst_sel XYZW, BUF_FMT_32_FLOAT, OOB_SELECT 3 = raw bounds check).
	inline void WriteBufferDescriptor(uint8_t* out, uint64_t address, uint64_t range)
	{
		const uint32_t v[4] = {
			static_cast<uint32_t>(address),
			static_cast<uint32_t>(address >> 32) & 0xFFFFu,
			static_cast<uint32_t>(std::min<uint64_t>(range, 0xFFFFFFFFu)),
			0x31014FACu,
		};
		std::memcpy(out, v, sizeof(v));
	}

	// Vertex buffer binding as AMDVLK writes it in the vertex buffer table (wddm-dump
	// [draw][re][vertex-input]): the binding's stride, NUM_RECORDS in vertices (OOB_SELECT 1 =
	// structured bounds check on the index); the fetch format is in the shader's tbuffer_load.
	// A zero stride falls back to a raw byte range.
	inline void WriteVertexBufferDescriptor(uint8_t* out, uint64_t address, uint64_t bytes, uint32_t stride)
	{
		const uint32_t v[4] = {
			static_cast<uint32_t>(address),
			(static_cast<uint32_t>(address >> 32) & 0xFFFFu) | ((stride & 0x3FFFu) << 16),
			static_cast<uint32_t>(std::min<uint64_t>(stride ? bytes / stride : bytes, 0xFFFFFFFFu)),
			stride ? 0x11014FACu : 0x31014FACu,
		};
		std::memcpy(out, v, sizeof(v));
	}

	// The attribute ring as PAL's VertexAttributeRing sets it up (gfx9ShaderRing.cpp): 16-byte
	// stride, swizzle_enable 3, index stride 32B, BUF_FMT_32_32_32_32_FLOAT.
	inline void WriteAttributeRingDescriptor(uint8_t* out, uint64_t address, uint64_t sizeBytes)
	{
		constexpr uint32_t kStride = 16;
		const uint32_t v[4] = {
			static_cast<uint32_t>(address),
			(static_cast<uint32_t>(address >> 32) & 0xFFFFu) | (kStride << 16) | (3u << 30),
			static_cast<uint32_t>(sizeBytes / kStride),
			0x2043FFACu,
		};
		std::memcpy(out, v, sizeof(v));
	}

	struct ImageFormat
	{
		uint32_t imgFmt; // IMG_FMT_* (GFX11 numbering)
		uint32_t swizzle[4]; // SQ_SEL_* returning R, G, B, A from the format's channels
	};

	// Vulkan formats the TA can read linearly, with their GFX11 IMG_FMT. AMD names packed formats
	// most significant field first and X is the least significant channel.
	inline bool GetImageFormat(VkFormat format, ImageFormat& out)
	{
		static constexpr uint32_t xyzw[4] = {kSelX, kSelY, kSelZ, kSelW};
		static constexpr uint32_t zyxw[4] = {kSelZ, kSelY, kSelX, kSelW};
		static constexpr uint32_t xy01[4] = {kSelX, kSelY, kSel0, kSel1};
		static constexpr uint32_t x001[4] = {kSelX, kSel0, kSel0, kSel1};
		static constexpr uint32_t xyz1[4] = {kSelX, kSelY, kSelZ, kSel1};
		static constexpr uint32_t zyx1[4] = {kSelZ, kSelY, kSelX, kSel1};
		static constexpr uint32_t wzyx[4] = {kSelW, kSelZ, kSelY, kSelX};
		static constexpr uint32_t yzwx[4] = {kSelY, kSelZ, kSelW, kSelX};
		static constexpr uint32_t yx01[4] = {kSelY, kSelX, kSel0, kSel1};
		struct Entry
		{
			VkFormat format;
			uint32_t imgFmt;
			const uint32_t* swizzle;
		};
		static const Entry kFormats[] = {
			{VK_FORMAT_R8_UNORM, 0x01, x001},
			{VK_FORMAT_R8_SNORM, 0x02, x001},
			{VK_FORMAT_R8_USCALED, 0x03, x001},
			{VK_FORMAT_R8_SSCALED, 0x04, x001},
			{VK_FORMAT_R8_UINT, 0x05, x001},
			{VK_FORMAT_R8_SINT, 0x06, x001},
			{VK_FORMAT_R8_SRGB, 0x40, x001},
			{VK_FORMAT_R16_UNORM, 0x07, x001},
			{VK_FORMAT_R16_SNORM, 0x08, x001},
			{VK_FORMAT_R16_USCALED, 0x09, x001},
			{VK_FORMAT_R16_SSCALED, 0x0A, x001},
			{VK_FORMAT_R16_UINT, 0x0B, x001},
			{VK_FORMAT_R16_SINT, 0x0C, x001},
			{VK_FORMAT_R16_SFLOAT, 0x0D, x001},
			{VK_FORMAT_D16_UNORM, 0x07, x001},
			{VK_FORMAT_R8G8_UNORM, 0x0E, xy01},
			{VK_FORMAT_R8G8_SNORM, 0x0F, xy01},
			{VK_FORMAT_R8G8_USCALED, 0x10, xy01},
			{VK_FORMAT_R8G8_SSCALED, 0x11, xy01},
			{VK_FORMAT_R8G8_UINT, 0x12, xy01},
			{VK_FORMAT_R8G8_SINT, 0x13, xy01},
			{VK_FORMAT_R8G8_SRGB, 0x41, xy01},
			{VK_FORMAT_R32_UINT, 0x14, x001},
			{VK_FORMAT_R32_SINT, 0x15, x001},
			{VK_FORMAT_R32_SFLOAT, 0x16, x001},
			{VK_FORMAT_D32_SFLOAT, 0x16, x001},
			{VK_FORMAT_R16G16_UNORM, 0x17, xy01},
			{VK_FORMAT_R16G16_SNORM, 0x18, xy01},
			{VK_FORMAT_R16G16_USCALED, 0x19, xy01},
			{VK_FORMAT_R16G16_SSCALED, 0x1A, xy01},
			{VK_FORMAT_R16G16_UINT, 0x1B, xy01},
			{VK_FORMAT_R16G16_SINT, 0x1C, xy01},
			{VK_FORMAT_R16G16_SFLOAT, 0x1D, xy01},
			{VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0x1E, xyz1}, // 10_11_11_FLOAT
			{VK_FORMAT_A2B10G10R10_UNORM_PACK32, 0x24, xyzw}, // 2_10_10_10
			{VK_FORMAT_A2B10G10R10_SNORM_PACK32, 0x25, xyzw},
			{VK_FORMAT_A2B10G10R10_USCALED_PACK32, 0x26, xyzw},
			{VK_FORMAT_A2B10G10R10_SSCALED_PACK32, 0x27, xyzw},
			{VK_FORMAT_A2B10G10R10_UINT_PACK32, 0x28, xyzw},
			{VK_FORMAT_A2B10G10R10_SINT_PACK32, 0x29, xyzw},
			{VK_FORMAT_A2R10G10B10_UNORM_PACK32, 0x24, zyxw},
			{VK_FORMAT_A2R10G10B10_SNORM_PACK32, 0x25, zyxw},
			{VK_FORMAT_A2R10G10B10_USCALED_PACK32, 0x26, zyxw},
			{VK_FORMAT_A2R10G10B10_SSCALED_PACK32, 0x27, zyxw},
			{VK_FORMAT_A2R10G10B10_UINT_PACK32, 0x28, zyxw},
			{VK_FORMAT_A2R10G10B10_SINT_PACK32, 0x29, zyxw},
			{VK_FORMAT_R8G8B8A8_UNORM, 0x2A, xyzw},
			{VK_FORMAT_R8G8B8A8_SNORM, 0x2B, xyzw},
			{VK_FORMAT_R8G8B8A8_USCALED, 0x2C, xyzw},
			{VK_FORMAT_R8G8B8A8_SSCALED, 0x2D, xyzw},
			{VK_FORMAT_R8G8B8A8_UINT, 0x2E, xyzw},
			{VK_FORMAT_R8G8B8A8_SINT, 0x2F, xyzw},
			{VK_FORMAT_R8G8B8A8_SRGB, 0x42, xyzw},
			{VK_FORMAT_A8B8G8R8_UNORM_PACK32, 0x2A, xyzw},
			{VK_FORMAT_A8B8G8R8_SNORM_PACK32, 0x2B, xyzw},
			{VK_FORMAT_A8B8G8R8_USCALED_PACK32, 0x2C, xyzw},
			{VK_FORMAT_A8B8G8R8_SSCALED_PACK32, 0x2D, xyzw},
			{VK_FORMAT_A8B8G8R8_UINT_PACK32, 0x2E, xyzw},
			{VK_FORMAT_A8B8G8R8_SINT_PACK32, 0x2F, xyzw},
			{VK_FORMAT_A8B8G8R8_SRGB_PACK32, 0x42, xyzw},
			{VK_FORMAT_B8G8R8A8_UNORM, 0x2A, zyxw},
			{VK_FORMAT_B8G8R8A8_SNORM, 0x2B, zyxw},
			{VK_FORMAT_B8G8R8A8_USCALED, 0x2C, zyxw},
			{VK_FORMAT_B8G8R8A8_SSCALED, 0x2D, zyxw},
			{VK_FORMAT_B8G8R8A8_UINT, 0x2E, zyxw},
			{VK_FORMAT_B8G8R8A8_SINT, 0x2F, zyxw},
			{VK_FORMAT_B8G8R8A8_SRGB, 0x42, zyxw},
			{VK_FORMAT_R32G32_UINT, 0x30, xy01},
			{VK_FORMAT_R32G32_SINT, 0x31, xy01},
			{VK_FORMAT_R32G32_SFLOAT, 0x32, xy01},
			{VK_FORMAT_R16G16B16A16_UNORM, 0x33, xyzw},
			{VK_FORMAT_R16G16B16A16_SNORM, 0x34, xyzw},
			{VK_FORMAT_R16G16B16A16_USCALED, 0x35, xyzw},
			{VK_FORMAT_R16G16B16A16_SSCALED, 0x36, xyzw},
			{VK_FORMAT_R16G16B16A16_UINT, 0x37, xyzw},
			{VK_FORMAT_R16G16B16A16_SINT, 0x38, xyzw},
			{VK_FORMAT_R16G16B16A16_SFLOAT, 0x39, xyzw},
			{VK_FORMAT_R32G32B32_UINT, 0x3A, xyz1},
			{VK_FORMAT_R32G32B32_SINT, 0x3B, xyz1},
			{VK_FORMAT_R32G32B32_SFLOAT, 0x3C, xyz1},
			{VK_FORMAT_R32G32B32A32_UINT, 0x3D, xyzw},
			{VK_FORMAT_R32G32B32A32_SINT, 0x3E, xyzw},
			{VK_FORMAT_R32G32B32A32_SFLOAT, 0x3F, xyzw},
			{VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 0x43, xyz1}, // 5_9_9_9_FLOAT
			{VK_FORMAT_R5G6B5_UNORM_PACK16, 0x44, zyx1}, // 5_6_5
			{VK_FORMAT_B5G6R5_UNORM_PACK16, 0x44, xyz1},
			{VK_FORMAT_A1R5G5B5_UNORM_PACK16, 0x45, zyxw}, // 1_5_5_5
			{VK_FORMAT_R5G5B5A1_UNORM_PACK16, 0x46, wzyx}, // 5_5_5_1
			{VK_FORMAT_B5G5R5A1_UNORM_PACK16, 0x46, yzwx},
			{VK_FORMAT_R4G4B4A4_UNORM_PACK16, 0x47, wzyx}, // 4_4_4_4
			{VK_FORMAT_B4G4R4A4_UNORM_PACK16, 0x47, yzwx},
			{VK_FORMAT_R4G4_UNORM_PACK8, 0x48, yx01}, // 4_4
		};
		for (const Entry& e : kFormats)
		{
			if (e.format == format)
			{
				out.imgFmt = e.imgFmt;
				std::copy(e.swizzle, e.swizzle + 4, out.swizzle);
				return true;
			}
		}
		return false;
	}

	inline uint32_t ComposeSwizzle(VkComponentSwizzle component, uint32_t channel, const uint32_t (&formatSwizzle)[4])
	{
		switch (component)
		{
			case VK_COMPONENT_SWIZZLE_ZERO:
				return kSel0;
			case VK_COMPONENT_SWIZZLE_ONE:
				return kSel1;
			case VK_COMPONENT_SWIZZLE_R:
				return formatSwizzle[0];
			case VK_COMPONENT_SWIZZLE_G:
				return formatSwizzle[1];
			case VK_COMPONENT_SWIZZLE_B:
				return formatSwizzle[2];
			case VK_COMPONENT_SWIZZLE_A:
				return formatSwizzle[3];
			default:
				return formatSwizzle[channel]; // IDENTITY
		}
	}

	struct ImageView
	{
		uint64_t address; // first texel of the view (base mip, base layer), 256-byte aligned
		VkFormat format;
		VkImageViewType viewType;
		uint32_t width; // of the base mip
		uint32_t height;
		uint32_t depth; // 3D: depth of the base mip; arrays/cubes: layer count
		VkComponentMapping components;
	};

	// T# of a linear view of one mip level (Vkd's mips are laid out by Vkd, not by the hardware's
	// linear mip chain, so a view starts at its base mip and has no other). The hardware derives the
	// row pitch from the width (256-byte aligned rows, AmdDevice::GetImageRowPitchAlignment) and the
	// layer pitch from the height. Returns false for a format the TA can't read.
	inline bool WriteImageDescriptor(uint8_t* out, const ImageView& view)
	{
		ImageFormat format{};
		if (!GetImageFormat(view.format, format) || (view.address & 0xFF) != 0)
			return false;
		uint32_t type = 9; // SQ_RSRC_IMG_2D
		switch (view.viewType)
		{
			case VK_IMAGE_VIEW_TYPE_1D:
				type = 8;
				break;
			case VK_IMAGE_VIEW_TYPE_2D:
				type = 9;
				break;
			case VK_IMAGE_VIEW_TYPE_3D:
				type = 10;
				break;
			case VK_IMAGE_VIEW_TYPE_CUBE:
				type = 11;
				break;
			case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
				type = 12;
				break;
			case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
				type = 13;
				break;
			case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
				type = 11;
				break;
			default:
				break;
		}
		const uint32_t width = std::max(view.width, 1u) - 1;
		const uint32_t height = std::max(view.height, 1u) - 1;
		const uint32_t depth = std::max(view.depth, 1u) - 1;
		const VkComponentSwizzle components[4] = {view.components.r, view.components.g, view.components.b, view.components.a};
		uint32_t sel[4];
		for (uint32_t c = 0; c < 4; ++c)
			sel[c] = ComposeSwizzle(components[c], c, format.swizzle);

		const uint32_t t[8] = {
			static_cast<uint32_t>(view.address >> 8),
			(static_cast<uint32_t>(view.address >> 40) & 0xFFu) | (format.imgFmt << 20) | ((width & 3u) << 30),
			((width >> 2) & 0xFFFu) | ((height & 0x3FFFu) << 14),
			sel[0] | (sel[1] << 3) | (sel[2] << 6) | (sel[3] << 9) | (type << 28), // base/last level 0, SW_LINEAR
			depth & 0x1FFFu,
			4u << 20, // perf_mod, as AMDVLK
			1u << 10, // iterate_256, as AMDVLK
			0,
		};
		std::memcpy(out, t, sizeof(t));
		return true;
	}

	inline uint32_t AddressClamp(VkSamplerAddressMode mode)
	{
		switch (mode)
		{
			case VK_SAMPLER_ADDRESS_MODE_REPEAT:
				return 0; // SQ_TEX_WRAP
			case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
				return 1; // SQ_TEX_MIRROR
			case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
				return 2; // SQ_TEX_CLAMP_LAST_TEXEL
			case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
				return 3; // SQ_TEX_MIRROR_ONCE_LAST_TEXEL
			case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
				return 6; // SQ_TEX_CLAMP_BORDER
			default:
				return 2;
		}
	}

	// Unsigned 4.8 fixed point (LOD clamps).
	inline uint32_t LodU4F8(float lod)
	{
		return static_cast<uint32_t>(std::clamp(lod, 0.0f, 15.99609375f) * 256.0f);
	}

	// S# per PAL's CreateSamplerSrds, with what AMDVLK sets for a non-anisotropic sampler (no
	// perf_mip, aniso_override off): TRUNC_COORD when both filters are nearest.
	inline void WriteSamplerDescriptor(uint8_t* out, const VkSamplerCreateInfo& info)
	{
		const bool aniso = info.anisotropyEnable == VK_TRUE && info.maxAnisotropy > 1.0f;
		uint32_t anisoRatio = 0; // SQ_TEX_ANISO_RATIO_1
		if (aniso)
			anisoRatio = info.maxAnisotropy >= 16.0f ? 4 : info.maxAnisotropy >= 8.0f ? 3
													   : info.maxAnisotropy >= 4.0f	  ? 2
																					  : 1;
		const bool nearest = info.magFilter == VK_FILTER_NEAREST && info.minFilter == VK_FILTER_NEAREST;
		const uint32_t compare = info.compareEnable ? static_cast<uint32_t>(info.compareOp) & 7u : 0u;
		const uint32_t word0 = AddressClamp(info.addressModeU) | (AddressClamp(info.addressModeV) << 3) |
							   (AddressClamp(info.addressModeW) << 6) | (anisoRatio << 9) | (compare << 12) |
							   ((info.unnormalizedCoordinates ? 1u : 0u) << 15) | ((nearest ? 1u : 0u) << 27);
		const uint32_t word1 = LodU4F8(info.minLod) | (LodU4F8(info.maxLod) << 12);
		const int32_t lodBias = static_cast<int32_t>(std::lround(std::clamp(info.mipLodBias, -32.0f, 31.99609375f) * 256.0f));
		auto xyFilter = [&](VkFilter f)
		{ return (f == VK_FILTER_LINEAR ? 1u : 0u) | (aniso ? 2u : 0u); };
		const uint32_t mipFilter = info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR ? 2u : 1u;
		const uint32_t word2 = (static_cast<uint32_t>(lodBias) & 0x3FFFu) | (xyFilter(info.magFilter) << 20) |
							   (xyFilter(info.minFilter) << 22) | (mipFilter << 26);
		uint32_t border = 0; // SQ_TEX_BORDER_COLOR_TRANS_BLACK
		if (info.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK || info.borderColor == VK_BORDER_COLOR_INT_OPAQUE_BLACK)
			border = 1;
		else if (info.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE || info.borderColor == VK_BORDER_COLOR_INT_OPAQUE_WHITE)
			border = 2;
		const uint32_t s[4] = {word0, word1, word2, border << 30};
		std::memcpy(out, s, sizeof(s));
	}
} // namespace vkd::amd::gfx11

/**
 * @file ColorPacking.hpp
 * @brief VkClearColorValue <-> texel byte packing, shared by the software and AMD drivers
 * @date 2026-10-07
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vulkan.h>

namespace vkd
{
	// R/G/B/A -> index into VkClearColorValue's per-channel arrays, or -1 for a non-color component.
	inline int ComponentChannelIndex(VKU_FORMAT_COMPONENT_TYPE type)
	{
		switch (type)
		{
			case VKU_FORMAT_COMPONENT_TYPE_R:
				return 0;
			case VKU_FORMAT_COMPONENT_TYPE_G:
				return 1;
			case VKU_FORMAT_COMPONENT_TYPE_B:
				return 2;
			case VKU_FORMAT_COMPONENT_TYPE_A:
				return 3;
			default:
				return -1;
		}
	}

	// IEEE 754 binary16 -> binary32.
	inline float HalfToFloat(std::uint16_t half)
	{
		const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
		std::uint32_t exponent = (half >> 10) & 0x1Fu;
		std::uint32_t mantissa = half & 0x3FFu;

		std::uint32_t bits;
		if (exponent == 0 && mantissa == 0)
			bits = sign;
		else if (exponent == 0)
		{
			exponent = 1;
			while ((mantissa & 0x400u) == 0)
			{
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x3FFu;
			bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
		}
		else if (exponent == 0x1Fu)
			bits = sign | 0x7F800000u | (mantissa << 13);
		else
			bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);

		float value;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	// IEEE 754 binary32 -> binary16, round-to-nearest, subnormals flushed to zero.
	inline std::uint16_t FloatToHalf(float value)
	{
		std::uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));

		const std::uint32_t sign = (bits >> 16) & 0x8000u;
		const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
		const std::uint32_t mantissa = bits & 0x7FFFFFu;

		if (exponent <= 0)
			return static_cast<std::uint16_t>(sign);
		if (exponent >= 0x1F)
			return static_cast<std::uint16_t>(sign | 0x7C00u);

		return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
	}

	inline std::uint64_t UnsignedMaxForBits(std::uint32_t bits)
	{
		return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
	}

	inline std::int64_t SignedMaxForBits(std::uint32_t bits)
	{
		return bits >= 64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (bits - 1)) - 1;
	}

	inline std::int64_t SignedMinForBits(std::uint32_t bits)
	{
		return bits >= 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (bits - 1));
	}

	// False for compressed/multiplane/depth-stencil formats, shared-exponent/unsigned-float layouts,
	// and unpacked components that aren't whole bytes -- what PackColor can't express.
	inline bool IsSupportedColorFormat(VkFormat format)
	{
		if (vkuFormatIsCompressed(format) || vkuFormatIsMultiplane(format) || vkuFormatIsDepthOrStencil(format))
			return false;

		if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
			return true;

		if (!vkuFormatIsUNORM(format) && !vkuFormatIsSRGB(format) && !vkuFormatIsSNORM(format) &&
			!vkuFormatIsUINT(format) && !vkuFormatIsSINT(format) && !vkuFormatIsSFLOAT(format) &&
			!vkuFormatIsUSCALED(format) && !vkuFormatIsSSCALED(format) && !vkuFormatIsUFLOAT(format))
			return false;

		const bool packed = vkuFormatIsPacked(format);
		const VKU_FORMAT_INFO info = vkuGetFormatInfo(format);
		std::uint32_t totalBits = 0;
		for (std::uint32_t i = 0; i < info.component_count; ++i)
		{
			const std::uint32_t bits = info.components[i].size;
			if (bits == 0 || bits > 64 || (!packed && bits % 8 != 0))
				return false;
			if (ComponentChannelIndex(info.components[i].type) < 0)
				return false;
			if (vkuFormatIsSFLOAT(format) && bits != 16 && bits != 32 && bits != 64)
				return false;
			totalBits += bits;
		}

		return totalBits == vkuFormatElementSize(format) * 8;
	}

	// Unsigned 10/11-bit float (5-bit exponent, bias 15, no sign), from the binary16 encoding with
	// its mantissa truncated (round toward zero, allowed for these conversions). Negative -> 0.
	inline std::uint32_t FloatToUnsignedSmallFloat(float value, std::uint32_t mantissaBits)
	{
		if (std::isnan(value))
			return (0x1Fu << mantissaBits) | 1u;
		if (!(value > 0.0f))
			return 0;
		const std::uint32_t half = FloatToHalf(value) & 0x7FFFu;
		return half >> (10 - mantissaBits);
	}

	// VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, with the spec's shared-exponent conversion.
	inline std::uint32_t PackSharedExponent(const float (&rgb)[3])
	{
		constexpr int kMantissaBits = 9;
		constexpr int kBias = 15;
		constexpr int kMaxExponent = 31;
		const float sharedMax = static_cast<float>((1 << kMantissaBits) - 1) / (1 << kMantissaBits) * std::ldexp(1.0f, kMaxExponent - kBias);
		float clamped[3];
		for (int i = 0; i < 3; ++i)
			clamped[i] = std::isnan(rgb[i]) ? 0.0f : std::clamp(rgb[i], 0.0f, sharedMax);
		const float maxC = std::max({clamped[0], clamped[1], clamped[2]});
		const int expP = std::max(-kBias - 1, maxC > 0.0f ? static_cast<int>(std::floor(std::log2(maxC))) : -kBias - 1) + 1 + kBias;
		const int maxS = static_cast<int>(std::floor(maxC / std::ldexp(1.0f, expP - kBias - kMantissaBits) + 0.5f));
		const int expS = maxS == (1 << kMantissaBits) ? expP + 1 : expP;
		std::uint32_t word = static_cast<std::uint32_t>(expS) << 27;
		for (int i = 0; i < 3; ++i)
		{
			const std::uint32_t mantissa = static_cast<std::uint32_t>(std::floor(clamped[i] / std::ldexp(1.0f, expS - kBias - kMantissaBits) + 0.5f));
			word |= (mantissa & 0x1FFu) << (9 * i);
		}
		return word;
	}

	// Raw bits of one component of a clear color, in the low `bits` bits.
	inline std::uint64_t EncodeColorComponent(VkFormat format, const VkClearColorValue& color, int channel, std::uint32_t bits)
	{
		const std::uint64_t mask = UnsignedMaxForBits(bits);
		if (vkuFormatIsUFLOAT(format))
			return FloatToUnsignedSmallFloat(color.float32[channel], bits - 5);
		// Scaled formats store integers: the clear value comes in uint32/int32 like UINT/SINT (the
		// CTS maps them to integer tcu formats and fills the integer members).
		if (vkuFormatIsUSCALED(format))
			return std::min(static_cast<std::uint64_t>(color.uint32[channel]), mask);
		if (vkuFormatIsSSCALED(format))
			return static_cast<std::uint64_t>(std::clamp(static_cast<std::int64_t>(color.int32[channel]), SignedMinForBits(bits), SignedMaxForBits(bits))) & mask;
		if (vkuFormatIsSFLOAT(format))
		{
			if (bits == 64)
			{
				const double value = color.float32[channel];
				std::uint64_t raw;
				std::memcpy(&raw, &value, sizeof(raw));
				return raw;
			}
			if (bits == 32)
			{
				std::uint32_t raw;
				std::memcpy(&raw, &color.float32[channel], sizeof(raw));
				return raw;
			}
			return FloatToHalf(color.float32[channel]);
		}
		if (vkuFormatIsUNORM(format) || vkuFormatIsSRGB(format))
		{
			float clamped = std::clamp(color.float32[channel], 0.0f, 1.0f);
			// sRGB formats store R, G and B encoded (the clear value is linear); alpha stays linear.
			if (vkuFormatIsSRGB(format) && channel != 3)
				clamped = clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
			return static_cast<std::uint64_t>(static_cast<double>(clamped) * static_cast<double>(mask) + 0.5) & mask;
		}
		if (vkuFormatIsSNORM(format))
		{
			const float clamped = std::clamp(color.float32[channel], -1.0f, 1.0f);
			return static_cast<std::uint64_t>(std::llround(static_cast<double>(clamped) * static_cast<double>(SignedMaxForBits(bits)))) & mask;
		}
		// 64-bit integer components take a pair of 32-bit clear values each: R = uint32[0..1],
		// G = uint32[2..3] (the CTS clear references for r64*/r64g64* formats).
		if (bits == 64)
			return channel < 2 ? (std::uint64_t{color.uint32[channel * 2 + 1]} << 32) | color.uint32[channel * 2] : 0;
		if (vkuFormatIsUINT(format))
			return std::min(static_cast<std::uint64_t>(color.uint32[channel]), mask);
		return static_cast<std::uint64_t>(std::clamp(static_cast<std::int64_t>(color.int32[channel]), SignedMinForBits(bits), SignedMaxForBits(bits))) & mask;
	}

	// Writes vkuFormatElementSize(format) bytes to outBytes; caller must have checked IsSupportedColorFormat(format) first.
	// Unpacked formats store each component in its own bytes, in component order; packed formats
	// are one little-endian word whose components are listed from the most significant bits down.
	inline void PackColor(VkFormat format, const VkClearColorValue& color, std::uint8_t* outBytes)
	{
		const VKU_FORMAT_INFO info = vkuGetFormatInfo(format);

		if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
		{
			const float rgb[3] = {color.float32[0], color.float32[1], color.float32[2]};
			const std::uint32_t word = PackSharedExponent(rgb);
			std::memcpy(outBytes, &word, sizeof(word));
			return;
		}

		if (vkuFormatIsPacked(format))
		{
			std::uint64_t word = 0;
			std::uint32_t shift = vkuFormatElementSize(format) * 8;
			for (std::uint32_t i = 0; i < info.component_count; ++i)
			{
				const std::uint32_t bits = info.components[i].size;
				shift -= bits;
				word |= EncodeColorComponent(format, color, ComponentChannelIndex(info.components[i].type), bits) << shift;
			}
			std::memcpy(outBytes, &word, vkuFormatElementSize(format));
			return;
		}

		std::size_t byteOffset = 0;
		for (std::uint32_t i = 0; i < info.component_count; ++i)
		{
			const std::uint32_t bits = info.components[i].size;
			const std::uint64_t value = EncodeColorComponent(format, color, ComponentChannelIndex(info.components[i].type), bits);
			std::memcpy(outBytes + byteOffset, &value, bits / 8);
			byteOffset += bits / 8;
		}
	}

	// Depth/stencil clear texel: writes vkuFormatElementSize(format) bytes to outBytes and marks in
	// outWriteMask the bytes the clear owns (only the cleared aspects' bytes of a combined format).
	// Linear Vkd storage: depth in the low bytes (D24 in bits 0-23 of a 32-bit word), stencil after.
	// Returns the texel size, 0 for a format this can't express.
	inline std::uint32_t PackDepthStencil(VkFormat format, const VkClearDepthStencilValue& value, VkImageAspectFlags aspects,
										  std::uint8_t* outBytes, std::uint8_t* outWriteMask)
	{
		const std::uint32_t size = vkuFormatElementSize(format);
		if (size == 0 || size > 8)
			return 0;
		std::memset(outBytes, 0, size);
		std::memset(outWriteMask, 0, size);

		const bool depth = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
		const bool stencil = (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
		const double d = std::clamp(static_cast<double>(value.depth), 0.0, 1.0);
		const std::uint8_t s = static_cast<std::uint8_t>(value.stencil);

		auto writeDepth = [&](const void* bytes, std::uint32_t count, std::uint32_t owned)
		{
			if (!depth)
				return;
			std::memcpy(outBytes, bytes, count);
			std::memset(outWriteMask, 0xFF, owned);
		};
		auto writeStencil = [&](std::uint32_t offset)
		{
			if (!stencil)
				return;
			outBytes[offset] = s;
			outWriteMask[offset] = 0xFF;
		};

		switch (format)
		{
			case VK_FORMAT_D16_UNORM:
			case VK_FORMAT_D16_UNORM_S8_UINT:
			{
				const std::uint16_t v = static_cast<std::uint16_t>(d * 65535.0 + 0.5);
				writeDepth(&v, 2, 2);
				if (format == VK_FORMAT_D16_UNORM_S8_UINT)
					writeStencil(2);
				return size;
			}
			case VK_FORMAT_X8_D24_UNORM_PACK32:
			case VK_FORMAT_D24_UNORM_S8_UINT:
			{
				const std::uint32_t v = static_cast<std::uint32_t>(d * 16777215.0 + 0.5);
				// X8_D24 owns its padding byte; D24S8's top byte is the stencil.
				writeDepth(&v, format == VK_FORMAT_X8_D24_UNORM_PACK32 ? 4 : 3, format == VK_FORMAT_X8_D24_UNORM_PACK32 ? 4 : 3);
				if (format == VK_FORMAT_D24_UNORM_S8_UINT)
					writeStencil(3);
				return size;
			}
			case VK_FORMAT_D32_SFLOAT:
			case VK_FORMAT_D32_SFLOAT_S8_UINT:
				writeDepth(&value.depth, 4, 4);
				if (format == VK_FORMAT_D32_SFLOAT_S8_UINT)
					writeStencil(4);
				return size;
			case VK_FORMAT_S8_UINT:
				writeStencil(0);
				return size;
			default:
				return 0;
		}
	}
} // namespace vkd

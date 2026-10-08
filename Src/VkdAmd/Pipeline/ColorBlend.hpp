#pragma once

#include <cstdint>
#include <iterator>
#include <span>

#include <vulkan/vulkan.h>

namespace vkd::amd
{
	// GFX11 color blend registers from the Vulkan pipeline state, as PAL computes them:
	// ColorBlendState::Init (RB+ on, no dual-source blending -- Vkd advertises neither dualSrcBlend
	// nor logicOp) and GraphicsPipeline::SetupCommonRegisters.
	inline constexpr uint32_t kMaxColorTargets = 8;
	inline constexpr uint32_t kCbTargetMask = 0xA08E;
	inline constexpr uint32_t kCbShaderMask = 0xA08F;
	inline constexpr uint32_t kCbBlendRed = 0xA105; // CB_BLEND_RED/GREEN/BLUE/ALPHA
	inline constexpr uint32_t kSxBlendOptControl = 0xA1D7;
	inline constexpr uint32_t kSxMrt0BlendOpt = 0xA1D8; // SX_MRT0..7_BLEND_OPT, then CB_BLEND0..7_CONTROL
	inline constexpr uint32_t kCbBlend0Control = 0xA1E0;
	inline constexpr uint32_t kCbColorControl = 0xA202;

	struct ColorBlendRegs
	{
		uint32_t sxMrtBlendOpt[kMaxColorTargets] = {};
		uint32_t cbBlendControl[kMaxColorTargets] = {};
		uint32_t cbTargetMask = 0;
		uint32_t sxBlendOptControl = 0;
	};

	namespace colorblend
	{
		// VkBlendFactor -> GFX11 BlendOp (the Vulkan order is PAL's Blend order; GFX11 renumbered
		// the constant and dual-source factors).
		inline constexpr uint32_t kHwBlend[] = {
			0x00, // ZERO
			0x01, // ONE
			0x02, // SRC_COLOR
			0x03, // ONE_MINUS_SRC_COLOR
			0x08, // DST_COLOR
			0x09, // ONE_MINUS_DST_COLOR
			0x04, // SRC_ALPHA
			0x05, // ONE_MINUS_SRC_ALPHA
			0x06, // DST_ALPHA
			0x07, // ONE_MINUS_DST_ALPHA
			0x0B, // CONSTANT_COLOR
			0x0C, // ONE_MINUS_CONSTANT_COLOR
			0x11, // CONSTANT_ALPHA
			0x12, // ONE_MINUS_CONSTANT_ALPHA
			0x0A, // SRC_ALPHA_SATURATE
			0x0D, // SRC1_COLOR
			0x0E, // ONE_MINUS_SRC1_COLOR
			0x0F, // SRC1_ALPHA
			0x10, // ONE_MINUS_SRC1_ALPHA
		};

		// VkBlendOp ADD, SUBTRACT, REVERSE_SUBTRACT, MIN, MAX -> CombFunc and SX_OPT_COMB_FCN.
		inline constexpr uint32_t kHwCombFunc[] = {0, 1, 4, 2, 3};
		inline constexpr uint32_t kSxCombFunc[] = {1, 2, 5, 3, 4};
		inline constexpr uint32_t kSxCombBlendDisabled = 6;

		// SX_BLEND_OPT for a color / alpha factor (PAL GetSxBlendOptColor/Alpha).
		inline constexpr uint32_t kSxOptPreserveAllIgnoreNone = 1;
		inline constexpr uint32_t kSxOptPreserveNoneIgnoreNone = 7;

		inline uint32_t SxOptColor(VkBlendFactor factor)
		{
			switch (factor)
			{
				case VK_BLEND_FACTOR_ZERO:
					return 0;
				case VK_BLEND_FACTOR_ONE:
					return 1;
				case VK_BLEND_FACTOR_SRC_COLOR:
					return 2;
				case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
					return 3;
				case VK_BLEND_FACTOR_SRC_ALPHA:
					return 4;
				case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
					return 5;
				case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
					return 6;
				default:
					return kSxOptPreserveNoneIgnoreNone;
			}
		}

		inline uint32_t SxOptAlpha(VkBlendFactor factor)
		{
			switch (factor)
			{
				case VK_BLEND_FACTOR_ZERO:
					return 0;
				case VK_BLEND_FACTOR_ONE:
				case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
					return 1;
				case VK_BLEND_FACTOR_SRC_COLOR:
				case VK_BLEND_FACTOR_SRC_ALPHA:
					return 4;
				case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
				case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
					return 5;
				default:
					return kSxOptPreserveNoneIgnoreNone;
			}
		}

		inline bool ReadsDestination(VkBlendFactor factor)
		{
			return factor == VK_BLEND_FACTOR_DST_COLOR || factor == VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR ||
				   factor == VK_BLEND_FACTOR_DST_ALPHA || factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		}

		inline bool IsMinMax(VkBlendOp op)
		{
			return op == VK_BLEND_OP_MIN || op == VK_BLEND_OP_MAX;
		}

		inline uint32_t HwBlend(VkBlendFactor factor)
		{
			return static_cast<uint32_t>(factor) < std::size(kHwBlend) ? kHwBlend[factor] : 0;
		}

		inline uint32_t Index(VkBlendOp op)
		{
			return static_cast<uint32_t>(op) < std::size(kHwCombFunc) ? static_cast<uint32_t>(op) : 0;
		}
	} // namespace colorblend

	// attachments: the pipeline's blend attachment states (empty when the spec says to ignore
	// pColorBlendState: rasterization disabled, no color attachment). Slots past them write nothing.
	inline ColorBlendRegs ComputeColorBlendRegs(std::span<const VkPipelineColorBlendAttachmentState> attachments)
	{
		using namespace colorblend;
		ColorBlendRegs regs;
		for (uint32_t i = 0; i < kMaxColorTargets; ++i)
		{
			const VkPipelineColorBlendAttachmentState att = i < attachments.size() ? attachments[i] : VkPipelineColorBlendAttachmentState{};
			const bool minMaxColor = IsMinMax(att.colorBlendOp);
			const bool minMaxAlpha = IsMinMax(att.alphaBlendOp);

			// CB_BLENDn_CONTROL: factors and functions, SEPARATE_ALPHA_BLEND always; MIN/MAX force
			// the factors to ONE.
			uint32_t cb = 1u << 29;
			if (att.blendEnable)
				cb |= 1u << 30;
			cb |= (minMaxColor ? 1u : HwBlend(att.srcColorBlendFactor)) << 0;
			cb |= kHwCombFunc[Index(att.colorBlendOp)] << 5;
			cb |= (minMaxColor ? 1u : HwBlend(att.dstColorBlendFactor)) << 8;
			cb |= (minMaxAlpha ? 1u : HwBlend(att.srcAlphaBlendFactor)) << 16;
			cb |= kHwCombFunc[Index(att.alphaBlendOp)] << 21;
			cb |= (minMaxAlpha ? 1u : HwBlend(att.dstAlphaBlendFactor)) << 24;
			regs.cbBlendControl[i] = cb;

			// SX_MRTn_BLEND_OPT: which source/destination components the blend can ignore (RB+).
			uint32_t sx = 0;
			if (att.blendEnable)
			{
				uint32_t colorSrc = SxOptColor(att.srcColorBlendFactor);
				uint32_t colorDst = (ReadsDestination(att.srcColorBlendFactor) || att.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE)
										? kSxOptPreserveNoneIgnoreNone
										: SxOptColor(att.dstColorBlendFactor);
				uint32_t alphaSrc = SxOptAlpha(att.srcAlphaBlendFactor);
				uint32_t alphaDst = ReadsDestination(att.srcAlphaBlendFactor) ? kSxOptPreserveNoneIgnoreNone : SxOptAlpha(att.dstAlphaBlendFactor);
				if (minMaxColor)
					colorSrc = colorDst = kSxOptPreserveAllIgnoreNone;
				if (minMaxAlpha)
					alphaSrc = alphaDst = kSxOptPreserveAllIgnoreNone;
				sx = colorSrc | (colorDst << 4) | (kSxCombFunc[Index(att.colorBlendOp)] << 8) | (alphaSrc << 16) | (alphaDst << 20) |
					 (kSxCombFunc[Index(att.alphaBlendOp)] << 24);
			}
			else
				sx = (kSxCombBlendDisabled << 8) | (kSxCombBlendDisabled << 24);
			regs.sxMrtBlendOpt[i] = sx;

			// CB_TARGET_MASK: 4 bits per target. SX_BLEND_OPT_CONTROL disables the SX optimization of
			// targets that write nothing (PAL SxBlendOptControl).
			const uint32_t writeMask = i < attachments.size() ? (att.colorWriteMask & 0xFu) : 0u;
			regs.cbTargetMask |= writeMask << (4 * i);
			if (writeMask == 0)
				regs.sxBlendOptControl |= 3u << (4 * i);
		}
		return regs;
	}

	// CB_COLOR_CONTROL: CB_NORMAL with the copy ROP3 when the shader exports to a written target,
	// CB_DISABLE otherwise; DISABLE_DUAL_QUAD 0 (RB+).
	inline uint32_t CbColorControl(uint32_t cbShaderMask, uint32_t cbTargetMask)
	{
		return (cbShaderMask == 0 || cbTargetMask == 0) ? 0u : (1u << 4) | (0xCCu << 16);
	}
} // namespace vkd::amd

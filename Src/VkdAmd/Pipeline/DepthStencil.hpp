#pragma once

#include <cstdint>
#include <cstring>

#include <vulkan/vulkan.h>

namespace vkd::amd
{
	// GFX11 depth/stencil registers: the pipeline's tests as PAL's DepthStencilState computes them
	// (SetupDbDepthControl / SetupDbStencilControl, stencil state from the Vulkan static values), and
	// the depth/stencil target as PAL's DepthStencilView for an image without HTILE.
	inline constexpr uint32_t kDbRenderControl = 0xA000;
	inline constexpr uint32_t kDbDepthView = 0xA002;
	inline constexpr uint32_t kDbRenderOverride = 0xA003;
	inline constexpr uint32_t kDbHtileDataBase = 0xA005;
	inline constexpr uint32_t kDbDepthSizeXy = 0xA007; // then DB_DEPTH_BOUNDS_MIN/MAX
	inline constexpr uint32_t kDbZInfo = 0xA010; // then DB_STENCIL_INFO, DB_Z/STENCIL_READ_BASE, DB_Z/STENCIL_WRITE_BASE
	inline constexpr uint32_t kDbZReadBaseHi = 0xA01A; // then DB_STENCIL_READ_BASE_HI, DB_Z/STENCIL_WRITE_BASE_HI, DB_HTILE_DATA_BASE_HI
	inline constexpr uint32_t kDbStencilControl = 0xA10B; // then DB_STENCILREFMASK, DB_STENCILREFMASK_BF
	inline constexpr uint32_t kDbDepthControl = 0xA200;
	inline constexpr uint32_t kPaSuPolyOffsetDbFmtCntl = 0xA2DE;
	inline constexpr uint32_t kPaSuPolyOffsetClamp = 0xA2DF; // then FRONT_SCALE, FRONT_OFFSET, BACK_SCALE, BACK_OFFSET
	inline constexpr uint32_t kPaSuScModeCntlPolyOffsetEnable = (1u << 11) | (1u << 12); // POLY_OFFSET_FRONT/BACK_ENABLE

	// Depth bias factors, as Vulkan gives them (static, or vkCmdSetDepthBias).
	struct DepthBias
	{
		float constantFactor = 0.0f;
		float clamp = 0.0f;
		float slopeFactor = 0.0f;
	};

	// Stencil test values of one face (static, or vkCmdSetStencilCompareMask/WriteMask/Reference).
	struct StencilFaceValues
	{
		uint32_t compareMask = 0;
		uint32_t writeMask = 0;
		uint32_t reference = 0;
	};

	struct DepthStencilRegs
	{
		uint32_t dbDepthControl = 0;
		uint32_t dbStencilControl = 0;
		uint32_t dbStencilRefMask = 0;
		uint32_t dbStencilRefMaskBf = 0;
		StencilFaceValues front; // the values behind dbStencilRefMask(Bf), for the dynamic ones to replace
		StencilFaceValues back;
		float depthBoundsMin = 0.0f;
		float depthBoundsMax = 1.0f;
	};

	namespace depthstencil
	{
		// VkStencilOp -> GFX11 StencilOp (KEEP, ZERO, REPLACE_TEST, ADD_CLAMP, SUB_CLAMP, INVERT,
		// ADD_WRAP, SUB_WRAP). VkCompareOp already is the hardware's CompareFrag/CompareRef order.
		inline constexpr uint32_t kHwStencilOp[] = {0, 1, 3, 5, 6, 7, 8, 9};

		inline uint32_t StencilOp(VkStencilOp op)
		{
			return static_cast<uint32_t>(op) < 8 ? kHwStencilOp[op] : 0;
		}

		// STENCILTESTVAL, STENCILMASK, STENCILWRITEMASK, STENCILOPVAL = 1 (the increment/decrement step).
		inline uint32_t RefMask(const StencilFaceValues& s)
		{
			return (s.reference & 0xFFu) | ((s.compareMask & 0xFFu) << 8) | ((s.writeMask & 0xFFu) << 16) | (1u << 24);
		}

		inline uint32_t RefMask(const VkStencilOpState& s)
		{
			return RefMask(StencilFaceValues{s.compareMask, s.writeMask, s.reference});
		}
	} // namespace depthstencil

	// state may be null: no depth/stencil attachment (or rasterization discarded) -- tests off, the
	// function ALWAYS as AMDVLK leaves it.
	inline DepthStencilRegs ComputeDepthStencilRegs(const VkPipelineDepthStencilStateCreateInfo* state)
	{
		using namespace depthstencil;
		DepthStencilRegs regs;
		const bool depthTest = state && state->depthTestEnable;
		const bool stencilTest = state && state->stencilTestEnable;
		const uint32_t zFunc = depthTest ? static_cast<uint32_t>(state->depthCompareOp) & 7u : 7u;
		// STENCIL_ENABLE, Z_ENABLE, Z_WRITE_ENABLE (Vulkan writes depth only when it tests it),
		// DEPTH_BOUNDS_ENABLE, ZFUNC, BACKFACE_ENABLE (always on), STENCILFUNC, STENCILFUNC_BF.
		regs.dbDepthControl = (stencilTest ? 1u : 0u) | (depthTest ? 2u : 0u) | (depthTest && state->depthWriteEnable ? 4u : 0u) |
							  (state && state->depthBoundsTestEnable ? 8u : 0u) | (zFunc << 4) | (1u << 7);
		if (stencilTest)
		{
			regs.dbDepthControl |= ((static_cast<uint32_t>(state->front.compareOp) & 7u) << 8) | ((static_cast<uint32_t>(state->back.compareOp) & 7u) << 20);
			regs.dbStencilControl = StencilOp(state->front.failOp) | (StencilOp(state->front.passOp) << 4) | (StencilOp(state->front.depthFailOp) << 8) |
									(StencilOp(state->back.failOp) << 12) | (StencilOp(state->back.passOp) << 16) | (StencilOp(state->back.depthFailOp) << 20);
		}
		if (state)
		{
			regs.front = {state->front.compareMask, state->front.writeMask, state->front.reference};
			regs.back = {state->back.compareMask, state->back.writeMask, state->back.reference};
			regs.dbStencilRefMask = RefMask(state->front);
			regs.dbStencilRefMaskBf = RefMask(state->back);
			regs.depthBoundsMin = state->minDepthBounds;
			regs.depthBoundsMax = state->maxDepthBounds;
		}
		else
			regs.dbStencilRefMask = regs.dbStencilRefMaskBf = 1u << 24;
		return regs;
	}
} // namespace vkd::amd

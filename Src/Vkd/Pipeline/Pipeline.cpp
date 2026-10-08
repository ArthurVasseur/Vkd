/**
 * @file Pipeline.cpp
 * @brief Implementation of Vulkan pipeline
 * @date 2025-10-27
 */

#include "Vkd/Pipeline/Pipeline.hpp"

#include "Vkd/RenderPass/RenderPass.hpp"

namespace vkd
{
	namespace
	{
		bool IsDynamic(const VkPipelineDynamicStateCreateInfo* dynamicState, VkDynamicState state)
		{
			if (!dynamicState)
				return false;
			for (UInt32 i = 0; i < dynamicState->dynamicStateCount; ++i)
			{
				if (dynamicState->pDynamicStates[i] == state)
					return true;
			}
			return false;
		}

		const VkPipelineRenderingCreateInfo* FindRenderingInfo(const void* next)
		{
			for (const auto* it = static_cast<const VkBaseInStructure*>(next); it; it = it->pNext)
			{
				if (it->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
					return reinterpret_cast<const VkPipelineRenderingCreateInfo*>(it);
			}
			return nullptr;
		}
	} // namespace

	GraphicsPipelineStates GetGraphicsPipelineStates(const VkGraphicsPipelineCreateInfo& info)
	{
		VkShaderStageFlags stages = 0;
		for (UInt32 i = 0; i < info.stageCount; ++i)
			stages |= info.pStages[i].stage;

		GraphicsPipelineStates states;
		states.dynamic = info.pDynamicState;
		states.rasterization = info.pRasterizationState;

		if ((stages & VK_SHADER_STAGE_MESH_BIT_EXT) == 0)
		{
			states.inputAssembly = info.pInputAssemblyState;
			if (!IsDynamic(info.pDynamicState, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT))
				states.vertexInput = info.pVertexInputState;
		}

		constexpr VkShaderStageFlags TessellationStages = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if ((stages & TessellationStages) == TessellationStages)
			states.tessellation = info.pTessellationState;

		const bool rasterizationDisabled = info.pRasterizationState && info.pRasterizationState->rasterizerDiscardEnable == VK_TRUE &&
										   !IsDynamic(info.pDynamicState, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE);
		if (rasterizationDisabled)
			return states;

		states.viewport = info.pViewportState;
		states.multisample = info.pMultisampleState;

		// A render pass subpass "uses" an attachment unless it is VK_ATTACHMENT_UNUSED; with dynamic
		// rendering any color attachment slot counts (VUID-VkGraphicsPipelineCreateInfo-renderPass-06055).
		bool usesColor = false;
		bool usesDepthStencil = false;
		if (info.renderPass != VK_NULL_HANDLE)
		{
			VKD_FROM_HANDLE(RenderPass, renderPass, info.renderPass);
			const auto& subpasses = renderPass->GetSubpasses();
			if (info.subpass < subpasses.size())
			{
				const VkSubpassDescription& subpass = subpasses[info.subpass];
				for (UInt32 i = 0; i < subpass.colorAttachmentCount; ++i)
					usesColor |= subpass.pColorAttachments[i].attachment != VK_ATTACHMENT_UNUSED;
				usesDepthStencil = subpass.pDepthStencilAttachment && subpass.pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED;
			}
		}
		else if (const VkPipelineRenderingCreateInfo* rendering = FindRenderingInfo(info.pNext))
		{
			usesColor = rendering->colorAttachmentCount > 0;
			usesDepthStencil = rendering->depthAttachmentFormat != VK_FORMAT_UNDEFINED || rendering->stencilAttachmentFormat != VK_FORMAT_UNDEFINED;
		}

		if (usesColor)
			states.colorBlend = info.pColorBlendState;
		if (usesDepthStencil)
			states.depthStencil = info.pDepthStencilState;
		return states;
	}
} // namespace vkd

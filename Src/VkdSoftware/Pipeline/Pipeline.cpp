/**
 * @file Pipeline.cpp
 * @brief Implementation of software renderer pipeline
 * @date 2025-10-26
 */

#include "VkdSoftware/Pipeline/Pipeline.hpp"

#include "Vkd/Device/Device.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/ShaderModule/ShaderModule.hpp"
#include "VkdSoftware/Device/Device.hpp"

#include "See/Executor/Executor.hpp"

namespace vkd::software
{
	namespace
	{
		see::ShaderStage ToSeeShaderStage(VkShaderStageFlagBits stage)
		{
			switch (stage)
			{
				case VK_SHADER_STAGE_VERTEX_BIT:
					return see::ShaderStage::Vertex;
				case VK_SHADER_STAGE_FRAGMENT_BIT:
					return see::ShaderStage::Fragment;
				case VK_SHADER_STAGE_COMPUTE_BIT:
					return see::ShaderStage::Compute;
				default:
					CCT_ASSERT_FALSE("Unsupported shader stage");
					return see::ShaderStage::Vertex;
			}
		}

		ShaderStageState RegisterStage(SoftwareDevice& device, const VkPipelineShaderStageCreateInfo& stage)
		{
			VKD_FROM_HANDLE(ShaderModule, shaderModule, stage.module);
			see::ShaderHandle handle = device.GetShaderExecutor().RegisterShader(ToSeeShaderStage(stage.stage), shaderModule->GetCode(), stage.pName);
			return ShaderStageState{.m_stage = stage.stage, .m_handle = handle};
		}
	} // namespace

	VkResult Pipeline::CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateGraphicsPipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* softwareDevice = static_cast<SoftwareDevice*>(&owner);

		m_stages.reserve(info.stageCount);
		for (cct::UInt32 i = 0; i < info.stageCount; ++i)
			m_stages.push_back(RegisterStage(*softwareDevice, info.pStages[i]));

		if (info.pVertexInputState)
		{
			const VkPipelineVertexInputStateCreateInfo& state = *info.pVertexInputState;
			if (state.vertexBindingDescriptionCount > 0 && state.pVertexBindingDescriptions)
				m_vertexBindings.assign(state.pVertexBindingDescriptions, state.pVertexBindingDescriptions + state.vertexBindingDescriptionCount);
			if (state.vertexAttributeDescriptionCount > 0 && state.pVertexAttributeDescriptions)
				m_vertexAttributes.assign(state.pVertexAttributeDescriptions, state.pVertexAttributeDescriptions + state.vertexAttributeDescriptionCount);
		}

		if (info.pInputAssemblyState)
		{
			m_topology = info.pInputAssemblyState->topology;
			m_primitiveRestartEnable = info.pInputAssemblyState->primitiveRestartEnable;
		}

		if (info.pViewportState)
		{
			const VkPipelineViewportStateCreateInfo& state = *info.pViewportState;
			if (state.viewportCount > 0 && state.pViewports)
				m_viewports.assign(state.pViewports, state.pViewports + state.viewportCount);
			if (state.scissorCount > 0 && state.pScissors)
				m_scissors.assign(state.pScissors, state.pScissors + state.scissorCount);
		}

		if (info.pRasterizationState)
		{
			m_cullMode = info.pRasterizationState->cullMode;
			m_frontFace = info.pRasterizationState->frontFace;
			m_polygonMode = info.pRasterizationState->polygonMode;
		}

		if (info.pColorBlendState && info.pColorBlendState->attachmentCount > 0 && info.pColorBlendState->pAttachments)
		{
			const VkPipelineColorBlendStateCreateInfo& state = *info.pColorBlendState;
			m_colorBlendAttachments.assign(state.pAttachments, state.pAttachments + state.attachmentCount);
		}

		if (info.pMultisampleState)
			m_rasterizationSamples = info.pMultisampleState->rasterizationSamples;

		if (info.pDepthStencilState)
		{
			m_hasDepthStencilState = true;
			m_depthStencilState = *info.pDepthStencilState;
		}

		VKD_FROM_HANDLE(vkd::RenderPass, renderPassObj, info.renderPass);
		m_renderPass = renderPassObj;
		m_subpass = info.subpass;

		return VK_SUCCESS;
	}

	VkResult Pipeline::CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateComputePipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* softwareDevice = static_cast<SoftwareDevice*>(&owner);
		m_stages.push_back(RegisterStage(*softwareDevice, info.stage));

		return VK_SUCCESS;
	}
} // namespace vkd::software

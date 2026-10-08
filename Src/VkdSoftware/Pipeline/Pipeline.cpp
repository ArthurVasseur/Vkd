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
	Pipeline::Pipeline(const VkAllocationCallbacks& allocationCallbacks) :
		vkd::Pipeline(allocationCallbacks),
		m_stages(ObjectAllocator<ShaderStageState>(allocationCallbacks)),
		m_vertexBindings(ObjectAllocator<VkVertexInputBindingDescription>(allocationCallbacks)),
		m_vertexAttributes(ObjectAllocator<VkVertexInputAttributeDescription>(allocationCallbacks)),
		m_viewports(ObjectAllocator<VkViewport>(allocationCallbacks)),
		m_scissors(ObjectAllocator<VkRect2D>(allocationCallbacks)),
		m_colorBlendAttachments(ObjectAllocator<VkPipelineColorBlendAttachmentState>(allocationCallbacks))
	{
	}

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

		const vkd::GraphicsPipelineStates states = vkd::GetGraphicsPipelineStates(info);
		if (states.vertexInput)
		{
			const VkPipelineVertexInputStateCreateInfo& state = *states.vertexInput;
			if (state.vertexBindingDescriptionCount > 0 && state.pVertexBindingDescriptions)
				m_vertexBindings.assign(state.pVertexBindingDescriptions, state.pVertexBindingDescriptions + state.vertexBindingDescriptionCount);
			if (state.vertexAttributeDescriptionCount > 0 && state.pVertexAttributeDescriptions)
				m_vertexAttributes.assign(state.pVertexAttributeDescriptions, state.pVertexAttributeDescriptions + state.vertexAttributeDescriptionCount);
		}

		if (states.inputAssembly)
		{
			m_topology = states.inputAssembly->topology;
			m_primitiveRestartEnable = states.inputAssembly->primitiveRestartEnable;
		}

		if (states.viewport)
		{
			const VkPipelineViewportStateCreateInfo& state = *states.viewport;
			if (state.viewportCount > 0 && state.pViewports)
				m_viewports.assign(state.pViewports, state.pViewports + state.viewportCount);
			if (state.scissorCount > 0 && state.pScissors)
				m_scissors.assign(state.pScissors, state.pScissors + state.scissorCount);
		}

		if (states.dynamic)
		{
			const VkPipelineDynamicStateCreateInfo& state = *states.dynamic;
			for (cct::UInt32 i = 0; i < state.dynamicStateCount; ++i)
			{
				if (state.pDynamicStates[i] == VK_DYNAMIC_STATE_VIEWPORT)
					m_viewportDynamic = true;
				else if (state.pDynamicStates[i] == VK_DYNAMIC_STATE_SCISSOR)
					m_scissorDynamic = true;
			}
		}

		if (states.rasterization)
		{
			m_cullMode = states.rasterization->cullMode;
			m_frontFace = states.rasterization->frontFace;
			m_polygonMode = states.rasterization->polygonMode;
		}

		if (states.colorBlend && states.colorBlend->attachmentCount > 0 && states.colorBlend->pAttachments)
		{
			const VkPipelineColorBlendStateCreateInfo& state = *states.colorBlend;
			m_colorBlendAttachments.assign(state.pAttachments, state.pAttachments + state.attachmentCount);
		}

		if (states.multisample)
			m_rasterizationSamples = states.multisample->rasterizationSamples;

		if (states.depthStencil)
		{
			m_hasDepthStencilState = true;
			m_depthStencilState = *states.depthStencil;
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

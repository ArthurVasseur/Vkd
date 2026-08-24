/**
 * @file Pipeline.cpp
 * @brief Implementation of software renderer pipeline
 * @date 2025-10-26
 */

#include "VkdSoftware/Pipeline/Pipeline.hpp"

#include "Vkd/Device/Device.hpp"
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

		void RegisterStage(SoftwareDevice& device, const VkPipelineShaderStageCreateInfo& stage)
		{
			VKD_FROM_HANDLE(ShaderModule, shaderModule, stage.module);
			(void)device.GetShaderExecutor().RegisterShader(ToSeeShaderStage(stage.stage), shaderModule->GetCode(), stage.pName);
		}
	} // namespace

	VkResult Pipeline::CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateGraphicsPipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* softwareDevice = static_cast<SoftwareDevice*>(&owner);
		for (cct::UInt32 i = 0; i < info.stageCount; ++i)
			RegisterStage(*softwareDevice, info.pStages[i]);

		// TODO: Add software-specific graphics pipeline initialization (viewport/scissor/topology/blend state, see TODO.md point 3)

		return VK_SUCCESS;
	}

	VkResult Pipeline::CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateComputePipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* softwareDevice = static_cast<SoftwareDevice*>(&owner);
		RegisterStage(*softwareDevice, info.stage);

		return VK_SUCCESS;
	}
} // namespace vkd::software

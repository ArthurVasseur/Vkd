//
// Created by arthur on 26/10/2025.
//

#include "VkdSoftware/Pipeline/Pipeline.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd::software
{
	VkResult Pipeline::CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateGraphicsPipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		// TODO: Add software-specific graphics pipeline initialization

		return VK_SUCCESS;
	}

	VkResult Pipeline::CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		// Call the base class implementation
		VkResult result = vkd::Pipeline::CreateComputePipeline(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		// TODO: Add software-specific compute pipeline initialization

		return VK_SUCCESS;
	}
}

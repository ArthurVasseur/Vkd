//
// Created by arthur on 26/10/2025.
//

#pragma once

#include "Vkd/Pipeline/Pipeline.hpp"

namespace vkd::software
{
	class Pipeline : public vkd::Pipeline
	{
	public:
		Pipeline() = default;
		~Pipeline() override = default;

		VkResult CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
		VkResult CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
}

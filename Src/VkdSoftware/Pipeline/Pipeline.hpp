/**
 * @file Pipeline.hpp
 * @brief Software renderer pipeline implementation
 * @date 2025-10-26
 *
 * Graphics pipeline implementation for CPU rendering.
 */

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
} // namespace vkd::software

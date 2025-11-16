/**
 * @file RenderPass.hpp
 * @brief Software renderer render pass implementation
 * @date 2025-11-16
 *
 * Render pass implementation for CPU rendering.
 */

#pragma once

#include "Vkd/RenderPass/RenderPass.hpp"

namespace vkd::software
{
	class RenderPass : public vkd::RenderPass
	{
	public:
		RenderPass() = default;
		~RenderPass() override = default;

		VkResult Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
} // namespace vkd::software

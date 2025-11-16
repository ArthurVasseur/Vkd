/**
 * @file RenderPass.cpp
 * @brief Implementation of software render pass
 * @date 2025-11-16
 */

#include "VkdSoftware/RenderPass/RenderPass.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult RenderPass::Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		return vkd::RenderPass::Create(owner, info, allocationCallbacks);
	}
} // namespace vkd::software

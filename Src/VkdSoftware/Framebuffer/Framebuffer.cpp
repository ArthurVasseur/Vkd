/**
 * @file Framebuffer.cpp
 * @brief Implementation of software framebuffer
 * @date 2025-11-21
 */

#include "VkdSoftware/Framebuffer/Framebuffer.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult Framebuffer::Create(Device& owner, const VkFramebufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		return vkd::Framebuffer::Create(owner, info, allocationCallbacks);
	}
} // namespace vkd::software

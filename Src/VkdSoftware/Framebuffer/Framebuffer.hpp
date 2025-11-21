/**
 * @file Framebuffer.hpp
 * @brief Software renderer framebuffer implementation
 * @date 2025-11-21
 *
 * Framebuffer implementation for CPU rendering.
 */

#pragma once

#include "Vkd/Framebuffer/Framebuffer.hpp"

namespace vkd::software
{
	class Framebuffer : public vkd::Framebuffer
	{
	public:
		Framebuffer() = default;
		~Framebuffer() override = default;

		VkResult Create(Device& owner, const VkFramebufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
} // namespace vkd::software

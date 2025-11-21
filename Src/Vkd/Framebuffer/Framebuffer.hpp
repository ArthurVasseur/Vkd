/**
 * @file Framebuffer.hpp
 * @brief Vulkan framebuffer abstraction
 * @date 2025-11-21
 *
 * Represents a framebuffer object containing attachments for rendering.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Framebuffer : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_FRAMEBUFFER;
		VKD_NON_DISPATCHABLE_HANDLE(Framebuffer);

		Framebuffer();
		~Framebuffer() override = default;

		virtual VkResult Create(Device& owner, const VkFramebufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkRenderPass GetRenderPass() const;
		[[nodiscard]] inline const std::vector<VkImageView>& GetAttachments() const;
		[[nodiscard]] inline cct::UInt32 GetWidth() const;
		[[nodiscard]] inline cct::UInt32 GetHeight() const;
		[[nodiscard]] inline cct::UInt32 GetLayers() const;

	protected:
		Device* m_owner;
		VkRenderPass m_renderPass;
		std::vector<VkImageView> m_attachments;
		cct::UInt32 m_width;
		cct::UInt32 m_height;
		cct::UInt32 m_layers;
	};
} // namespace vkd

#include "Framebuffer.inl"

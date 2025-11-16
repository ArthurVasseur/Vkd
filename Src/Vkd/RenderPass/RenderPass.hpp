/**
 * @file RenderPass.hpp
 * @brief Vulkan render pass abstraction
 * @date 2025-11-16
 *
 * Represents a render pass object describing the structure and dependencies
 * of attachments used in rendering operations.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class RenderPass : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_RENDER_PASS;
		VKD_NON_DISPATCHABLE_HANDLE(RenderPass);

		RenderPass();
		~RenderPass() override = default;

		virtual VkResult Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline const std::vector<VkAttachmentDescription>& GetAttachments() const;
		[[nodiscard]] inline const std::vector<VkSubpassDescription>& GetSubpasses() const;
		[[nodiscard]] inline const std::vector<VkSubpassDependency>& GetDependencies() const;

	protected:
		Device* m_owner;
		std::vector<VkAttachmentDescription> m_attachments;
		std::vector<VkSubpassDescription> m_subpasses;
		std::vector<VkSubpassDependency> m_dependencies;
	};
} // namespace vkd

#include "RenderPass.inl"

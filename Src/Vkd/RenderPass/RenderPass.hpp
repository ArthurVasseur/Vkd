/**
 * @file RenderPass.hpp
 * @brief Vulkan render pass abstraction
 * @date 2025-11-16
 *
 * Represents a render pass object describing the structure and dependencies
 * of attachments used in rendering operations.
 */

#pragma once

#include "Vkd/Memory/Stl.hpp"
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

		explicit RenderPass(const VkAllocationCallbacks& allocationCallbacks);
		~RenderPass() override = default;

		virtual VkResult Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline const vkd::vector<VkAttachmentDescription>& GetAttachments() const;
		[[nodiscard]] inline const vkd::vector<VkSubpassDescription>& GetSubpasses() const;
		[[nodiscard]] inline const vkd::vector<VkSubpassDependency>& GetDependencies() const;

	protected:
		Device* m_owner;
		vkd::vector<VkAttachmentDescription> m_attachments;
		vkd::vector<VkSubpassDescription> m_subpasses;
		vkd::vector<VkSubpassDependency> m_dependencies;

		// Owned storage for the VkAttachmentReference/index arrays that m_subpasses' pInputAttachments,
		// pColorAttachments, pResolveAttachments, pDepthStencilAttachment and pPreserveAttachments point
		// into - VkSubpassDescription itself only borrows those pointers from the caller's create-info,
		// which isn't guaranteed to outlive this call, so they must be deep-copied to stay valid.
		vkd::vector<vkd::vector<VkAttachmentReference>> m_subpassInputAttachments;
		vkd::vector<vkd::vector<VkAttachmentReference>> m_subpassColorAttachments;
		vkd::vector<vkd::vector<VkAttachmentReference>> m_subpassResolveAttachments;
		vkd::vector<VkAttachmentReference> m_subpassDepthStencilAttachments;
		vkd::vector<vkd::vector<UInt32>> m_subpassPreserveAttachments;
	};
} // namespace vkd

#include "RenderPass.inl"

/**
 * @file RenderPass.inl
 * @brief Inline implementations for RenderPass
 * @date 2025-11-16
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"

namespace vkd
{
	inline RenderPass::RenderPass() :
		ObjectBase(ObjectType),
		m_owner(nullptr)
	{
	}

	inline VkResult RenderPass::Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;

		if (info.pAttachments)
			m_attachments.assign(info.pAttachments, info.pAttachments + info.attachmentCount);

		if (info.pSubpasses)
			m_subpasses.assign(info.pSubpasses, info.pSubpasses + info.subpassCount);

		if (info.pDependencies)
			m_dependencies.assign(info.pDependencies, info.pDependencies + info.dependencyCount);

		SetAllocationCallbacks(allocationCallbacks);

		return VK_SUCCESS;
	}

	inline Device* RenderPass::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline const std::vector<VkAttachmentDescription>& RenderPass::GetAttachments() const
	{
		AssertValid();
		return m_attachments;
	}

	inline const std::vector<VkSubpassDescription>& RenderPass::GetSubpasses() const
	{
		AssertValid();
		return m_subpasses;
	}

	inline const std::vector<VkSubpassDependency>& RenderPass::GetDependencies() const
	{
		AssertValid();
		return m_dependencies;
	}
} // namespace vkd

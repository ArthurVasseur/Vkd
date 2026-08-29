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
	inline RenderPass::RenderPass(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_attachments(ObjectAllocator<VkAttachmentDescription>(allocationCallbacks)),
		m_subpasses(ObjectAllocator<VkSubpassDescription>(allocationCallbacks)),
		m_dependencies(ObjectAllocator<VkSubpassDependency>(allocationCallbacks)),
		m_subpassInputAttachments(ObjectAllocator<vkd::vector<VkAttachmentReference>>(allocationCallbacks)),
		m_subpassColorAttachments(ObjectAllocator<vkd::vector<VkAttachmentReference>>(allocationCallbacks)),
		m_subpassResolveAttachments(ObjectAllocator<vkd::vector<VkAttachmentReference>>(allocationCallbacks)),
		m_subpassDepthStencilAttachments(ObjectAllocator<VkAttachmentReference>(allocationCallbacks)),
		m_subpassPreserveAttachments(ObjectAllocator<vkd::vector<UInt32>>(allocationCallbacks))
	{
	}

	inline VkResult RenderPass::Create(Device& owner, const VkRenderPassCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;

		if (info.pAttachments)
			m_attachments.assign(info.pAttachments, info.pAttachments + info.attachmentCount);

		if (info.pSubpasses)
		{
			m_subpasses.assign(info.pSubpasses, info.pSubpasses + info.subpassCount);

			const vkd::vector<VkAttachmentReference> emptyReferences(ObjectAllocator<VkAttachmentReference>(allocationCallbacks));
			const vkd::vector<UInt32> emptyIndices(ObjectAllocator<UInt32>(allocationCallbacks));

			m_subpassInputAttachments.resize(info.subpassCount, emptyReferences);
			m_subpassColorAttachments.resize(info.subpassCount, emptyReferences);
			m_subpassResolveAttachments.resize(info.subpassCount, emptyReferences);
			m_subpassDepthStencilAttachments.resize(info.subpassCount);
			m_subpassPreserveAttachments.resize(info.subpassCount, emptyIndices);

			for (UInt32 i = 0; i < info.subpassCount; ++i)
			{
				const VkSubpassDescription& src = info.pSubpasses[i];
				VkSubpassDescription& dst = m_subpasses[i];

				if (src.pInputAttachments && src.inputAttachmentCount > 0)
				{
					m_subpassInputAttachments[i].assign(src.pInputAttachments, src.pInputAttachments + src.inputAttachmentCount);
					dst.pInputAttachments = m_subpassInputAttachments[i].data();
				}

				if (src.pColorAttachments && src.colorAttachmentCount > 0)
				{
					m_subpassColorAttachments[i].assign(src.pColorAttachments, src.pColorAttachments + src.colorAttachmentCount);
					dst.pColorAttachments = m_subpassColorAttachments[i].data();
				}

				if (src.pResolveAttachments && src.colorAttachmentCount > 0)
				{
					m_subpassResolveAttachments[i].assign(src.pResolveAttachments, src.pResolveAttachments + src.colorAttachmentCount);
					dst.pResolveAttachments = m_subpassResolveAttachments[i].data();
				}

				if (src.pDepthStencilAttachment)
				{
					m_subpassDepthStencilAttachments[i] = *src.pDepthStencilAttachment;
					dst.pDepthStencilAttachment = &m_subpassDepthStencilAttachments[i];
				}

				if (src.pPreserveAttachments && src.preserveAttachmentCount > 0)
				{
					m_subpassPreserveAttachments[i].assign(src.pPreserveAttachments, src.pPreserveAttachments + src.preserveAttachmentCount);
					dst.pPreserveAttachments = m_subpassPreserveAttachments[i].data();
				}
			}
		}

		if (info.pDependencies)
			m_dependencies.assign(info.pDependencies, info.pDependencies + info.dependencyCount);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* RenderPass::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline const vkd::vector<VkAttachmentDescription>& RenderPass::GetAttachments() const
	{
		AssertValid();
		return m_attachments;
	}

	inline const vkd::vector<VkSubpassDescription>& RenderPass::GetSubpasses() const
	{
		AssertValid();
		return m_subpasses;
	}

	inline const vkd::vector<VkSubpassDependency>& RenderPass::GetDependencies() const
	{
		AssertValid();
		return m_dependencies;
	}
} // namespace vkd

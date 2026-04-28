/**
 * @file Framebuffer.cpp
 * @brief Implementation of Framebuffer
 * @date 2025-11-21
 */

#include "Vkd/Framebuffer/Framebuffer.hpp"

namespace vkd
{
	VkResult Framebuffer::Create(Device& owner, const VkFramebufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_renderPass = info.renderPass;
		m_width = info.width;
		m_height = info.height;
		m_layers = info.layers;
		SetAllocationCallbacks(allocationCallbacks);

		if (info.attachmentCount > 0 && info.pAttachments != nullptr)
		{
			m_attachments.resize(info.attachmentCount);
			for (UInt32 i = 0; i < info.attachmentCount; ++i)
				m_attachments[i] = info.pAttachments[i];
		}

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}
} // namespace vkd

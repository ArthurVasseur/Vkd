namespace vkd
{
	inline Framebuffer::Framebuffer() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_renderPass(VK_NULL_HANDLE),
		m_width(0),
		m_height(0),
		m_layers(0)
	{
	}

	inline Device* Framebuffer::GetOwner() const
	{
		return m_owner;
	}

	inline VkRenderPass Framebuffer::GetRenderPass() const
	{
		return m_renderPass;
	}

	inline const std::vector<VkImageView>& Framebuffer::GetAttachments() const
	{
		return m_attachments;
	}

	inline cct::UInt32 Framebuffer::GetWidth() const
	{
		return m_width;
	}

	inline cct::UInt32 Framebuffer::GetHeight() const
	{
		return m_height;
	}

	inline cct::UInt32 Framebuffer::GetLayers() const
	{
		return m_layers;
	}
} // namespace vkd

namespace vkd::software
{
	inline const vkd::vector<ShaderStageState>& Pipeline::GetStages() const
	{
		return m_stages;
	}

	inline see::ShaderHandle Pipeline::GetShaderHandle(VkShaderStageFlagBits stage) const
	{
		for (const ShaderStageState& stageState : m_stages)
			if (stageState.m_stage == stage)
				return stageState.m_handle;

		return see::InvalidShaderHandle;
	}

	inline const vkd::vector<VkVertexInputBindingDescription>& Pipeline::GetVertexBindings() const
	{
		return m_vertexBindings;
	}

	inline const vkd::vector<VkVertexInputAttributeDescription>& Pipeline::GetVertexAttributes() const
	{
		return m_vertexAttributes;
	}

	inline VkPrimitiveTopology Pipeline::GetTopology() const
	{
		return m_topology;
	}

	inline VkBool32 Pipeline::GetPrimitiveRestartEnable() const
	{
		return m_primitiveRestartEnable;
	}

	inline const vkd::vector<VkViewport>& Pipeline::GetViewports() const
	{
		return m_viewports;
	}

	inline const vkd::vector<VkRect2D>& Pipeline::GetScissors() const
	{
		return m_scissors;
	}

	inline bool Pipeline::IsViewportDynamic() const
	{
		return m_viewportDynamic;
	}

	inline bool Pipeline::IsScissorDynamic() const
	{
		return m_scissorDynamic;
	}

	inline VkCullModeFlags Pipeline::GetCullMode() const
	{
		return m_cullMode;
	}

	inline VkFrontFace Pipeline::GetFrontFace() const
	{
		return m_frontFace;
	}

	inline VkPolygonMode Pipeline::GetPolygonMode() const
	{
		return m_polygonMode;
	}

	inline const vkd::vector<VkPipelineColorBlendAttachmentState>& Pipeline::GetColorBlendAttachments() const
	{
		return m_colorBlendAttachments;
	}

	inline VkSampleCountFlagBits Pipeline::GetRasterizationSamples() const
	{
		return m_rasterizationSamples;
	}

	inline bool Pipeline::HasDepthStencilState() const
	{
		return m_hasDepthStencilState;
	}

	inline const VkPipelineDepthStencilStateCreateInfo& Pipeline::GetDepthStencilState() const
	{
		return m_depthStencilState;
	}

	inline vkd::RenderPass* Pipeline::GetRenderPass() const
	{
		return m_renderPass;
	}

	inline UInt32 Pipeline::GetSubpass() const
	{
		return m_subpass;
	}
} // namespace vkd::software

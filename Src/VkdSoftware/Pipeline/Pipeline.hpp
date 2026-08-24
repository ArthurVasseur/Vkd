/**
 * @file Pipeline.hpp
 * @brief Software renderer pipeline implementation
 * @date 2025-10-26
 *
 * Graphics pipeline implementation for CPU rendering.
 */

#pragma once

#include <vector>

#include "Vkd/Pipeline/Pipeline.hpp"

#include "See/Executor/Executor.hpp"

namespace vkd
{
	class RenderPass;
}

namespace vkd::software
{
	struct ShaderStageState
	{
		VkShaderStageFlagBits m_stage;
		see::ShaderHandle m_handle;
	};

	class Pipeline : public vkd::Pipeline
	{
	public:
		Pipeline() = default;
		~Pipeline() override = default;

		VkResult CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
		VkResult CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;

		[[nodiscard]] inline const std::vector<ShaderStageState>& GetStages() const;
		[[nodiscard]] inline see::ShaderHandle GetShaderHandle(VkShaderStageFlagBits stage) const;

		[[nodiscard]] inline const std::vector<VkVertexInputBindingDescription>& GetVertexBindings() const;
		[[nodiscard]] inline const std::vector<VkVertexInputAttributeDescription>& GetVertexAttributes() const;

		[[nodiscard]] inline VkPrimitiveTopology GetTopology() const;
		[[nodiscard]] inline VkBool32 GetPrimitiveRestartEnable() const;

		[[nodiscard]] inline const std::vector<VkViewport>& GetViewports() const;
		[[nodiscard]] inline const std::vector<VkRect2D>& GetScissors() const;

		[[nodiscard]] inline VkCullModeFlags GetCullMode() const;
		[[nodiscard]] inline VkFrontFace GetFrontFace() const;
		[[nodiscard]] inline VkPolygonMode GetPolygonMode() const;

		[[nodiscard]] inline const std::vector<VkPipelineColorBlendAttachmentState>& GetColorBlendAttachments() const;

		[[nodiscard]] inline VkSampleCountFlagBits GetRasterizationSamples() const;

		[[nodiscard]] inline bool HasDepthStencilState() const;
		[[nodiscard]] inline const VkPipelineDepthStencilStateCreateInfo& GetDepthStencilState() const;

		[[nodiscard]] inline vkd::RenderPass* GetRenderPass() const;
		[[nodiscard]] inline UInt32 GetSubpass() const;

	private:
		std::vector<ShaderStageState> m_stages;

		std::vector<VkVertexInputBindingDescription> m_vertexBindings;
		std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;

		VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkBool32 m_primitiveRestartEnable = VK_FALSE;

		std::vector<VkViewport> m_viewports;
		std::vector<VkRect2D> m_scissors;

		VkCullModeFlags m_cullMode = VK_CULL_MODE_NONE;
		VkFrontFace m_frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		VkPolygonMode m_polygonMode = VK_POLYGON_MODE_FILL;

		std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachments;

		VkSampleCountFlagBits m_rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		bool m_hasDepthStencilState = false;
		VkPipelineDepthStencilStateCreateInfo m_depthStencilState = {};

		vkd::RenderPass* m_renderPass = nullptr;
		UInt32 m_subpass = 0;
	};
} // namespace vkd::software

#include "VkdSoftware/Pipeline/Pipeline.inl"

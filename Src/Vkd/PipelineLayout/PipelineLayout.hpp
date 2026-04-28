/**
 * @file PipelineLayout.hpp
 * @brief Vulkan pipeline layout abstraction
 * @date 2026-04-28
 *
 * Stores the descriptor set layouts and push constant ranges that
 * describe the resource interface of a pipeline.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class PipelineLayout : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
		VKD_NON_DISPATCHABLE_HANDLE(PipelineLayout);

		PipelineLayout();
		~PipelineLayout() override = default;

		virtual VkResult Create(Device& owner, const VkPipelineLayoutCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkPipelineLayoutCreateFlags GetFlags() const;
		[[nodiscard]] inline const std::vector<VkDescriptorSetLayout>& GetSetLayouts() const;
		[[nodiscard]] inline const std::vector<VkPushConstantRange>& GetPushConstantRanges() const;

	protected:
		Device* m_owner;
		VkPipelineLayoutCreateFlags m_flags;
		std::vector<VkDescriptorSetLayout> m_setLayouts;
		std::vector<VkPushConstantRange> m_pushConstantRanges;
	};
} // namespace vkd

#include "PipelineLayout.inl"

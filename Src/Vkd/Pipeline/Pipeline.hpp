/**
 * @file Pipeline.hpp
 * @brief Vulkan graphics pipeline abstraction
 * @date 2025-10-26
 *
 * Represents a graphics pipeline object.
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Pipeline : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_PIPELINE;
		VKD_NON_DISPATCHABLE_HANDLE(Pipeline);

		Pipeline();
		~Pipeline() override = default;

		virtual VkResult CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);
		virtual VkResult CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkPipelineBindPoint GetBindPoint() const;
		[[nodiscard]] inline VkPipelineLayout GetLayout() const;

	protected:
		Device* m_owner;
		VkPipelineBindPoint m_bindPoint;
		VkPipelineLayout m_layout;
	};
} // namespace vkd

#include "Pipeline.inl"

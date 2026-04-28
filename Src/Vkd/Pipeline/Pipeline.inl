/**
 * @file Pipeline.inl
 * @brief Inline implementations for Pipeline
 * @date 2025-10-26
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"

namespace vkd
{
	inline Pipeline::Pipeline() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_bindPoint(VK_PIPELINE_BIND_POINT_MAX_ENUM),
		m_layout(VK_NULL_HANDLE)
	{
	}

	inline VkResult Pipeline::CreateGraphicsPipeline(Device& owner, const VkGraphicsPipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		m_layout = info.layout;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline VkResult Pipeline::CreateComputePipeline(Device& owner, const VkComputePipelineCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		m_layout = info.layout;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* Pipeline::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkPipelineBindPoint Pipeline::GetBindPoint() const
	{
		AssertValid();
		return m_bindPoint;
	}

	inline VkPipelineLayout Pipeline::GetLayout() const
	{
		AssertValid();
		return m_layout;
	}
} // namespace vkd

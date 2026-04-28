/**
 * @file PipelineLayout.inl
 * @brief Inline implementations for PipelineLayout
 * @date 2026-04-28
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/PipelineLayout/PipelineLayout.hpp"

namespace vkd
{
	inline PipelineLayout::PipelineLayout() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_flags(0)
	{
	}

	inline VkResult PipelineLayout::Create(Device& owner, const VkPipelineLayoutCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_flags = info.flags;

		if (info.setLayoutCount > 0 && info.pSetLayouts != nullptr)
			m_setLayouts.assign(info.pSetLayouts, info.pSetLayouts + info.setLayoutCount);

		if (info.pushConstantRangeCount > 0 && info.pPushConstantRanges != nullptr)
			m_pushConstantRanges.assign(info.pPushConstantRanges, info.pPushConstantRanges + info.pushConstantRangeCount);

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* PipelineLayout::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkPipelineLayoutCreateFlags PipelineLayout::GetFlags() const
	{
		AssertValid();
		return m_flags;
	}

	inline const std::vector<VkDescriptorSetLayout>& PipelineLayout::GetSetLayouts() const
	{
		AssertValid();
		return m_setLayouts;
	}

	inline const std::vector<VkPushConstantRange>& PipelineLayout::GetPushConstantRanges() const
	{
		AssertValid();
		return m_pushConstantRanges;
	}
} // namespace vkd

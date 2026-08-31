/**
 * @file DescriptorSet.inl
 * @brief Inline implementations for DescriptorSet
 * @date 2026-08-25
 */

#pragma once

#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline DescriptorSet::DescriptorSet(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_layout(nullptr),
		m_bufferBindings(ObjectAllocator<std::pair<const UInt32, vkd::vector<BufferBinding>>>(allocationCallbacks)),
		m_imageBindings(ObjectAllocator<std::pair<const UInt32, vkd::vector<ImageBinding>>>(allocationCallbacks)),
		m_bufferViewBindings(ObjectAllocator<std::pair<const UInt32, vkd::vector<BufferView*>>>(allocationCallbacks))
	{
	}

	inline VkResult DescriptorSet::Create(Device& owner, DescriptorSetLayout& layout, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_layout = &layout;

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* DescriptorSet::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline DescriptorSetLayout* DescriptorSet::GetLayout() const
	{
		AssertValid();
		return m_layout;
	}
} // namespace vkd

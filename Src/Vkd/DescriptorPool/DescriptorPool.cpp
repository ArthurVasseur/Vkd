/**
 * @file DescriptorPool.cpp
 * @brief Implementation of DescriptorPool
 * @date 2026-08-25
 */

#include "Vkd/DescriptorPool/DescriptorPool.hpp"

#include <algorithm>

#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	Result<DescriptorSet*, VkResult> DescriptorPool::AllocateSet(DescriptorSetLayout& layout, const VkAllocationCallbacks& allocationCallbacks)
	{
		AssertValid();

		auto* set = mem::New<DescriptorSet>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!set)
			return VK_ERROR_OUT_OF_HOST_MEMORY;

		VkResult result = set->Create(*m_owner, layout, allocationCallbacks);
		if (result != VK_SUCCESS)
		{
			mem::Delete(allocationCallbacks, set);
			return result;
		}

		m_allocatedSets.push_back(set);
		return set;
	}

	void DescriptorPool::FreeSet(DescriptorSet* set, const VkAllocationCallbacks& allocationCallbacks)
	{
		AssertValid();

		if (!set)
			return;

		auto it = std::find(m_allocatedSets.begin(), m_allocatedSets.end(), set);
		if (it != m_allocatedSets.end())
			m_allocatedSets.erase(it);

		mem::Delete(allocationCallbacks, set);
	}

	void DescriptorPool::Reset(const VkAllocationCallbacks& allocationCallbacks)
	{
		AssertValid();

		for (DescriptorSet* set : m_allocatedSets)
			mem::Delete(allocationCallbacks, set);

		m_allocatedSets.clear();
	}
} // namespace vkd

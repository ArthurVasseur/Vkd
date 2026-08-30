/**
 * @file QueryPool.inl
 * @brief Inline implementations for QueryPool
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"

namespace vkd
{
	inline QueryPool::QueryPool(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_queryType(VK_QUERY_TYPE_OCCLUSION),
		m_queryCount(0),
		m_pipelineStatistics(0),
		m_results(ObjectAllocator<UInt64>(allocationCallbacks)),
		m_available(ObjectAllocator<UInt8>(allocationCallbacks))
	{
	}

	inline Device* QueryPool::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkQueryType QueryPool::GetQueryType() const
	{
		AssertValid();
		return m_queryType;
	}

	inline UInt32 QueryPool::GetQueryCount() const
	{
		AssertValid();
		return m_queryCount;
	}
} // namespace vkd

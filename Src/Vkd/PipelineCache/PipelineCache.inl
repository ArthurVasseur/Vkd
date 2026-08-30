/**
 * @file PipelineCache.inl
 * @brief Inline implementations for PipelineCache
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/PipelineCache/PipelineCache.hpp"

namespace vkd
{
	inline PipelineCache::PipelineCache(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_header{}
	{
	}

	inline Device* PipelineCache::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}
} // namespace vkd

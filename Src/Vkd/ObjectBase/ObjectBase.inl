/**
 * @file ObjectBase.inl
 * @brief Inline implementations for ObjectBase
 * @date 2025-04-23
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	inline ObjectBase::ObjectBase(VkObjectType objectType) :
		m_allocationCallbacks(nullptr),
		m_objectType(objectType)
	{
	}

	inline VkObjectType ObjectBase::GetObjectType() const
	{
		return m_objectType;
	}

	const VkAllocationCallbacks& ObjectBase::GetAllocationCallbacks() const
	{
		return *m_allocationCallbacks;
	}

	inline void ObjectBase::SetAllocationCallbacks(const VkAllocationCallbacks& allocationCallbacks)
	{
		m_allocationCallbacks = &allocationCallbacks;
	}
}

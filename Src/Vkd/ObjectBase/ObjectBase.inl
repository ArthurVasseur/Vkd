//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	inline ObjectBase::ObjectBase(VkObjectType objectType) :
		m_AllocationCallbacks(nullptr),
		m_objectType(objectType)
	{
	}

	inline VkObjectType ObjectBase::GetObjectType() const
	{
		return m_objectType;
	}

	const VkAllocationCallbacks* ObjectBase::GetAllocationCallbacks() const
	{
		return m_AllocationCallbacks;
	}

	inline void ObjectBase::SetAllocationCallbacks(const VkAllocationCallbacks* allocationCallbacks)
	{
		m_AllocationCallbacks = allocationCallbacks;
	}
}

/**
 * @file ObjectBase.inl
 * @brief Inline implementations for ObjectBase
 * @date 2025-04-23
 */

#pragma once

#ifdef VKD_DEBUG_CHECKS
#include <cpptrace/cpptrace.hpp>
#endif // VKD_DEBUG_CHECKS

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	inline ObjectBase::ObjectBase(VkObjectType objectType) :
		m_allocationCallbacks(nullptr),
		m_objectType(objectType),
		m_createResult(VK_ERROR_UNKNOWN)
	{
	}

	inline bool ObjectBase::IsValid() const
	{
		return m_createResult == VK_SUCCESS;
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

	inline void ObjectBase::AssertValid() const
	{
#ifdef VKD_DEBUG_CHECKS
		CCT_ASSERT(IsValid(), "Error, object '{}' is not in a valid state.\n{}", GetClassName(), cpptrace::generate_trace().to_string());
#endif // VKD_DEBUG_CHECKS
	}
} // namespace vkd

//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Defines.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	class ObjectBase
	{
	public:
		ObjectBase(VkObjectType objectType);

		inline VkObjectType GetObjectType() const;
		[[nodiscard]] inline const VkAllocationCallbacks* GetAllocationCallbacks() const;
		inline void SetAllocationCallbacks(const VkAllocationCallbacks* allocationCallbacks);
	private:
		const VkAllocationCallbacks* m_AllocationCallbacks;
		VkObjectType m_objectType;
	};
}

#include "Vkd/ObjectBase/ObjectBase.inl"

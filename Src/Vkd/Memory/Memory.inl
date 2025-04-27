//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Memory/Memory.hpp"

namespace vkd::mem
{
	template <typename T>
	T* Allocate(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope)
	{
		return static_cast<T*>(pAllocator->pfnAllocation(pAllocator->pUserData, sizeof(T), alignof(T), allocationScope));
	}

	inline void Free(const VkAllocationCallbacks* pAllocator, void* memory)
	{
		pAllocator->pfnFree(pAllocator->pUserData, memory);
	}


	template <typename T>
	T* New(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope)
	{
		T* allocation = Allocate<T>(pAllocator, allocationScope);

		return new (allocation) T;
	}

	template <typename T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope)
	{
		auto* dispatchableObject = Allocate<DispatchableObject<T>>(pAllocator, allocationScope);

		dispatchableObject->loaderData.loaderMagic = ICD_LOADER_MAGIC;
		new (&dispatchableObject->object) T;
		return dispatchableObject;
	}

	template <typename T>
	void Delete(T* object)
	{
		const VkAllocationCallbacks* pAllocator = object->object.GetAllocationCallbacks();
		object->~T();
		Free(pAllocator, object);
	}
}

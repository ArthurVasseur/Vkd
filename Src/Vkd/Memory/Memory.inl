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
	requires std::is_base_of_v<ObjectBase, T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope)
	{
		auto* dispatchableObject = Allocate<DispatchableObject<T>>(pAllocator, allocationScope);

		dispatchableObject->LoaderData.loaderMagic = ICD_LOADER_MAGIC;
		new (&dispatchableObject->Object) T;
		return dispatchableObject;
	}

	template <typename T>
	requires std::is_base_of_v<ObjectBase, T>
	void DeleteDispatchable(DispatchableObject<T>* object)
	{
		const VkAllocationCallbacks* pAllocator = object->Object.GetAllocationCallbacks();
		object->Object.~T();
		Free(pAllocator, object);
	}
}

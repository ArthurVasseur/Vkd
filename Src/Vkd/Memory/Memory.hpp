//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Defines.hpp"

namespace vkd::mem
{
	template<typename T>
	struct DispatchableObject
	{
		VK_LOADER_DATA loaderData;
		T object;
	};

	template<typename T>
	T* Allocate(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope);

	inline void Free(const VkAllocationCallbacks* pAllocator, void* memory);

	template<typename T>
	T* New(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope);

	template<typename T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks* pAllocator, VkSystemAllocationScope allocationScope);

	template<typename T>
	void Delete(T* object);
}

#include "Vkd/Memory/Memory.inl"
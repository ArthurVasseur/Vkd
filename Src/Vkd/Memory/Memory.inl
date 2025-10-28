//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Memory/Memory.hpp"

namespace vkd::mem
{
	template <typename T>
	T* Allocate(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope)
	{
		return static_cast<T*>(pAllocator.pfnAllocation(pAllocator.pUserData, sizeof(T), alignof(T), allocationScope));
	}

	inline void Free(const VkAllocationCallbacks& pAllocator, void* memory)
	{
		pAllocator.pfnFree(pAllocator.pUserData, memory);
	}


	template <typename T>
	T* New(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope)
	{
		T* allocation = Allocate<T>(pAllocator, allocationScope);

		return new (allocation) T;
	}

	template <typename T, typename... Args>
	T* New(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope, Args&&... args)
	{
		T* allocation = Allocate<T>(pAllocator, allocationScope);

		return new (allocation) T(std::forward<Args>(args)...);
	}

	template<typename T>
	void Delete(const VkAllocationCallbacks& pAllocator, T* object)
	{
		VKD_CHECK(object);
		object->~T();
		Free(pAllocator, object);
	}

	template <typename T>
	requires std::is_base_of_v<ObjectBase, T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope)
	{
		auto* dispatchableObject = Allocate<DispatchableObject<T>>(pAllocator, allocationScope);
		if (dispatchableObject == nullptr)
			return nullptr;

		dispatchableObject->LoaderData.loaderMagic = ICD_LOADER_MAGIC;
		dispatchableObject->Object = New<T>(pAllocator, allocationScope);

		return dispatchableObject;
	}

	template <typename T, typename... Args>
	requires std::is_base_of_v<ObjectBase, T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope, Args&&... args)
	{
		auto* dispatchableObject = Allocate<DispatchableObject<T>>(pAllocator, allocationScope);
		if (dispatchableObject == nullptr)
			return nullptr;

		dispatchableObject->LoaderData.loaderMagic = ICD_LOADER_MAGIC;
		dispatchableObject->Object = New<T>(pAllocator, allocationScope, std::forward<Args>(args)...);

		return dispatchableObject;
	}

	template <typename T>
	requires std::is_base_of_v<ObjectBase, T>
	void DeleteDispatchable(DispatchableObject<T>* object)
	{
		const VkAllocationCallbacks& pAllocator = object->Object->GetAllocationCallbacks();
		object->Object->~T();
		Free(pAllocator, object->Object);
		Free(pAllocator, object);
	}
}

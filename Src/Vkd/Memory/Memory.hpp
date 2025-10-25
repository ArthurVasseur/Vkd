//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Defines.hpp"
#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd::mem
{
	template<typename T>
	T* Allocate(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope);

	inline void Free(const VkAllocationCallbacks& pAllocator, void* memory);

	template<typename T>
	T* New(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope);

	template<typename T>
	requires std::is_base_of_v<ObjectBase, T>
	DispatchableObject<T>* NewDispatchable(const VkAllocationCallbacks& pAllocator, VkSystemAllocationScope allocationScope);

	template<typename T>
	requires std::is_base_of_v<ObjectBase, T>
	void DeleteDispatchable(DispatchableObject<T>* object);
}

#include "Vkd/Memory/Memory.inl"
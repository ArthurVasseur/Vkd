//
// Created by arthur on 23/04/2025.
//

#pragma once
#include "Vkd/Defines.hpp"

namespace vkd
{
	class ObjectBase
	{
	public:
		explicit ObjectBase(VkObjectType objectType);
		ObjectBase(ObjectBase&&) = default;
		ObjectBase(const ObjectBase&) = delete;
		virtual ~ObjectBase() = default;

		ObjectBase& operator=(ObjectBase&&) = default;
		ObjectBase& operator=(const ObjectBase&) = delete;

		[[nodiscard]] inline VkObjectType GetObjectType() const;
		[[nodiscard]] inline const VkAllocationCallbacks* GetAllocationCallbacks() const;
		inline void SetAllocationCallbacks(const VkAllocationCallbacks* allocationCallbacks);
	private:
		const VkAllocationCallbacks* m_allocationCallbacks;
		VkObjectType m_objectType;
	};

	template<typename T>
	requires std::is_base_of_v<ObjectBase, T>
	struct DispatchableObject
	{
		VK_LOADER_DATA LoaderData;
		T* Object;
	};
}

#include "Vkd/ObjectBase/ObjectBase.inl"

//Do not remove this include:
#include "Vkd/Memory/Memory.hpp"
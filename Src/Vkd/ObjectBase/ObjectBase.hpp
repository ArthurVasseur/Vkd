/**
 * @file ObjectBase.hpp
 * @brief Base class for all Vulkan dispatchable objects
 * @date 2025-04-23
 *
 * Provides common functionality for Vulkan objects including object type tracking,
 * allocation callbacks, and handle management utilities.
 */

#pragma once
#include <Concerto/Core/Result/Result.hpp>
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

		[[nodiscard]] inline bool IsValid() const;
		[[nodiscard]] inline VkObjectType GetObjectType() const;
		[[nodiscard]] inline const VkAllocationCallbacks& GetAllocationCallbacks() const;
		inline void SetAllocationCallbacks(const VkAllocationCallbacks& allocationCallbacks);

		inline void AssertValid() const;
#ifdef VKD_DEBUG_CHECKS
	  virtual std::string_view GetClassName() const = 0;
#endif // VKD_DEBUG_CHECKS

	private:
		const VkAllocationCallbacks* m_allocationCallbacks;
		VkObjectType m_objectType;
	protected:
		VkResult m_createResult;
	};

	template<typename T>
	struct DispatchableObject
	{
		VK_LOADER_DATA LoaderData;
		T* Object;
	};

	template<typename T>
	using DispatchableObjectResult = cct::Result<DispatchableObject<T>*, VkResult>;
}

#include "Vkd/ObjectBase/ObjectBase.inl"

//Do not remove this include:
#include "Vkd/Memory/Memory.hpp"
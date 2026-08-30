/**
 * @file Event.hpp
 * @brief Vulkan event
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Event : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_EVENT;
		VKD_NON_DISPATCHABLE_HANDLE(Event);

		explicit Event(const VkAllocationCallbacks& allocationCallbacks);
		~Event() override = default;

		virtual VkResult Create(Device& owner, const VkEventCreateInfo& createInfo);

		[[nodiscard]] inline Device* GetOwner() const;

		virtual VkResult GetStatus() = 0;
		virtual VkResult Set() = 0;
		virtual VkResult Reset() = 0;

	private:
		Device* m_owner;
	};
} // namespace vkd

#include "Vkd/Synchronization/Event/Event.inl"

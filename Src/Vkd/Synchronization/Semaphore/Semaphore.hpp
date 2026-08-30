/**
 * @file Semaphore.hpp
 * @brief Vulkan binary semaphore
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Semaphore : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_SEMAPHORE;
		VKD_NON_DISPATCHABLE_HANDLE(Semaphore);

		explicit Semaphore(const VkAllocationCallbacks& allocationCallbacks);
		~Semaphore() override = default;

		virtual VkResult Create(Device& owner, const VkSemaphoreCreateInfo& createInfo);

		[[nodiscard]] inline Device* GetOwner() const;

		virtual void Wait() = 0;
		virtual void Signal() = 0;

	private:
		Device* m_owner;
	};
} // namespace vkd

#include "Vkd/Synchronization/Semaphore/Semaphore.inl"

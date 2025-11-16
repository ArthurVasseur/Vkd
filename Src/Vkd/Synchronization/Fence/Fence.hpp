/**
 * @file Fence.hpp
 * @brief Vulkan fence synchronization primitive
 * @date 2025-10-25
 *
 * Represents a fence for GPU-CPU synchronization.
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Fence : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_FENCE;
		VKD_NON_DISPATCHABLE_HANDLE(Fence);

		Fence();
		~Fence() override = default;

		virtual VkResult Create(Device& owner, const VkFenceCreateInfo& createInfo);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkFenceCreateFlags GetFlags() const;

		// Vulkan API entry points

		virtual VkResult GetStatus() = 0;
		virtual VkResult Wait(uint64_t timeout) = 0;
		virtual VkResult Reset() = 0;
		virtual VkResult Signal() = 0;

	private:
		Device* m_owner;
		VkFenceCreateFlags m_flags;
	};
} // namespace vkd

#include "Fence.inl"

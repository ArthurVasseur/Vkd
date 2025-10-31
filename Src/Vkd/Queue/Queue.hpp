/**
 * @file Queue.hpp
 * @brief Vulkan queue abstraction
 * @date 2025-04-23
 *
 * Represents a device queue and handles command buffer submission and synchronization.
 */

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class Device;

	class Queue : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_QUEUE;
		VKD_DISPATCHABLE_HANDLE(Queue);

		Queue();
		~Queue() override = default;

		virtual VkResult Create(Device& owner, UInt32 queueFamilyIndex, UInt32 queueIndex, VkDeviceQueueCreateFlags flags);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline UInt32 GetQueueFamilyIndex() const;
		[[nodiscard]] inline UInt32 GetQueueIndex() const;
		[[nodiscard]] inline VkDeviceQueueCreateFlags GetFlags() const;

		// Vulkan API entry points
		static VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
		static VkResult VKAPI_CALL QueueWaitIdle(VkQueue queue);
		static VkResult VKAPI_CALL QueueBindSparse(VkQueue queue, uint32_t bindInfoCount,const VkBindSparseInfo* pBindInfo, VkFence fence);

	protected:
		virtual VkResult Submit(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) = 0;
		virtual VkResult WaitIdle() = 0;
		virtual VkResult BindSparse(uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) = 0;

	private:
		Device* m_owner;
		UInt32 m_queueFamilyIndex;
		UInt32 m_queueIndex;
		VkDeviceQueueCreateFlags m_flags;
	};
}

#include "Queue.inl"

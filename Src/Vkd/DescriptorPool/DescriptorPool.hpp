/**
 * @file DescriptorPool.hpp
 * @brief Vulkan descriptor pool abstraction
 * @date 2026-08-25
 *
 * Owns the DescriptorSet objects allocated from it, for vkResetDescriptorPool /
 * vkDestroyDescriptorPool to implicitly free them.
 */

#pragma once

#include <vector>

#include <Concerto/Core/Result/Result.hpp>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;
	class DescriptorSet;
	class DescriptorSetLayout;

	class DescriptorPool : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
		VKD_NON_DISPATCHABLE_HANDLE(DescriptorPool);

		DescriptorPool();
		~DescriptorPool() override = default;

		virtual VkResult Create(Device& owner, const VkDescriptorPoolCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkDescriptorPoolCreateFlags GetFlags() const;

		Result<DescriptorSet*, VkResult> AllocateSet(DescriptorSetLayout& layout, const VkAllocationCallbacks& allocationCallbacks);
		void FreeSet(DescriptorSet* set, const VkAllocationCallbacks& allocationCallbacks);
		void Reset(const VkAllocationCallbacks& allocationCallbacks);

	protected:
		Device* m_owner;
		VkDescriptorPoolCreateFlags m_flags;
		std::vector<DescriptorSet*> m_allocatedSets;
	};
} // namespace vkd

#include "DescriptorPool.inl"

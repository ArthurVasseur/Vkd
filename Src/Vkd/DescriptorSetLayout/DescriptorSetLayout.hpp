/**
 * @file DescriptorSetLayout.hpp
 * @brief Vulkan descriptor set layout abstraction
 * @date 2026-04-28
 *
 * Stores descriptor bindings (type, count, shader stages, immutable
 * samplers) describing the resource layout of a single descriptor set.
 */

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class DescriptorSetLayout : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
		VKD_NON_DISPATCHABLE_HANDLE(DescriptorSetLayout);

		struct Binding
		{
			UInt32 binding;
			VkDescriptorType descriptorType;
			UInt32 descriptorCount;
			VkShaderStageFlags stageFlags;
			std::vector<VkSampler> immutableSamplers;
		};

		DescriptorSetLayout();
		~DescriptorSetLayout() override = default;

		virtual VkResult Create(Device& owner, const VkDescriptorSetLayoutCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkDescriptorSetLayoutCreateFlags GetFlags() const;
		[[nodiscard]] inline const std::vector<Binding>& GetBindings() const;
		[[nodiscard]] const Binding* FindBinding(UInt32 bindingIndex) const noexcept;

	protected:
		Device* m_owner;
		VkDescriptorSetLayoutCreateFlags m_flags;
		std::vector<Binding> m_bindings;
	};
} // namespace vkd

#include "DescriptorSetLayout.inl"

/**
 * @file DescriptorSetLayout.inl
 * @brief Inline implementations for DescriptorSetLayout
 * @date 2026-04-28
 */

#pragma once

#include "Vkd/DescriptorSetLayout/DescriptorSetLayout.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline DescriptorSetLayout::DescriptorSetLayout() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_flags(0)
	{
	}

	inline VkResult DescriptorSetLayout::Create(Device& owner, const VkDescriptorSetLayoutCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_flags = info.flags;

		SetAllocationCallbacks(allocationCallbacks);

		if (info.bindingCount > 0 && info.pBindings != nullptr)
		{
			m_bindings.reserve(info.bindingCount);
			for (UInt32 i = 0; i < info.bindingCount; ++i)
			{
				const VkDescriptorSetLayoutBinding& src = info.pBindings[i];
				Binding dst;
				dst.binding = src.binding;
				dst.descriptorType = src.descriptorType;
				dst.descriptorCount = src.descriptorCount;
				dst.stageFlags = src.stageFlags;

				const bool needsImmutable = src.pImmutableSamplers != nullptr && (src.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || src.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
				if (needsImmutable && src.descriptorCount > 0)
					dst.immutableSamplers.assign(src.pImmutableSamplers, src.pImmutableSamplers + src.descriptorCount);

				m_bindings.push_back(std::move(dst));
			}
		}

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* DescriptorSetLayout::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkDescriptorSetLayoutCreateFlags DescriptorSetLayout::GetFlags() const
	{
		AssertValid();
		return m_flags;
	}

	inline const std::vector<DescriptorSetLayout::Binding>& DescriptorSetLayout::GetBindings() const
	{
		AssertValid();
		return m_bindings;
	}
} // namespace vkd

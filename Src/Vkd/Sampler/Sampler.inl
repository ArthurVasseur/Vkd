/**
 * @file Sampler.inl
 * @brief Inline implementations for Sampler
 * @date 2026-08-25
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/Sampler/Sampler.hpp"

namespace vkd
{
	inline Sampler::Sampler() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_magFilter(VK_FILTER_NEAREST),
		m_minFilter(VK_FILTER_NEAREST),
		m_addressModeU(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
		m_addressModeV(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
	{
	}

	inline VkResult Sampler::Create(Device& owner, const VkSamplerCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_magFilter = info.magFilter;
		m_minFilter = info.minFilter;
		m_addressModeU = info.addressModeU;
		m_addressModeV = info.addressModeV;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* Sampler::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkFilter Sampler::GetMagFilter() const
	{
		AssertValid();
		return m_magFilter;
	}

	inline VkFilter Sampler::GetMinFilter() const
	{
		AssertValid();
		return m_minFilter;
	}

	inline VkSamplerAddressMode Sampler::GetAddressModeU() const
	{
		AssertValid();
		return m_addressModeU;
	}

	inline VkSamplerAddressMode Sampler::GetAddressModeV() const
	{
		AssertValid();
		return m_addressModeV;
	}
} // namespace vkd

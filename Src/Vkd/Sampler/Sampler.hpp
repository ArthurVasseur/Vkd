/**
 * @file Sampler.hpp
 * @brief Vulkan sampler abstraction
 * @date 2026-08-25
 *
 * Stores the filtering/addressing state describing how a texture should be sampled.
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class Sampler : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_SAMPLER;
		VKD_NON_DISPATCHABLE_HANDLE(Sampler);

		explicit Sampler(const VkAllocationCallbacks& allocationCallbacks);
		~Sampler() override = default;

		virtual VkResult Create(Device& owner, const VkSamplerCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkFilter GetMagFilter() const;
		[[nodiscard]] inline VkFilter GetMinFilter() const;
		[[nodiscard]] inline VkSamplerAddressMode GetAddressModeU() const;
		[[nodiscard]] inline VkSamplerAddressMode GetAddressModeV() const;
		// Every VkSamplerCreateInfo field (pNext dropped): what a GPU sampler descriptor encodes.
		[[nodiscard]] inline const VkSamplerCreateInfo& GetCreateInfo() const;

	protected:
		Device* m_owner;
		VkFilter m_magFilter;
		VkFilter m_minFilter;
		VkSamplerAddressMode m_addressModeU;
		VkSamplerAddressMode m_addressModeV;
		VkSamplerCreateInfo m_createInfo;
	};
} // namespace vkd

#include "Sampler.inl"

/**
 * @file PhysicalDevice.hpp
 * @brief AMD physical device implementation
 * @date 2026-08-31
 *
 * Properties and queue families reversed from amdvlk64.dll
 * Instance_InitPhysicalDevices @ 0x18023ec40.
 */

#pragma once

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

#include "VkdAmd/Wddm/WddmAdapter.hpp"

namespace vkd::amd
{
	class PhysicalDevice : public vkd::PhysicalDevice
	{
	public:
		using vkd::PhysicalDevice::PhysicalDevice;
		~PhysicalDevice() override = default;

		VkResult Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks) override;
		VkResult Create(Instance& owner, const wddm::AdapterInfo& adapter, const VkAllocationCallbacks& allocationCallbacks);

		DispatchableObjectResult<vkd::Device> CreateDevice() override;

		// Transfers are CP DMA byte copies and clears CP fills (no shaders yet), so every color format
		// whose clear value PackColor encodes exactly is a transfer format here, in 1D/2D/3D.
		void GetFormatProperties(VkFormat format, VkFormatProperties& properties) const override;
		VkResult GetImageFormatProperties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
										  VkImageCreateFlags flags, VkImageFormatProperties& properties) const override;

		[[nodiscard]] const wddm::AdapterInfo& GetAdapterInfo() const
		{
			return m_adapter;
		}

	private:
		wddm::AdapterInfo m_adapter = {};
	};
} // namespace vkd::amd

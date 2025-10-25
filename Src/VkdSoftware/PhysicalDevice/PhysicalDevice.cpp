//
// Created by arthur on 25/10/2025.
//

#include "VkdSoftware/PhysicalDevice/PhysicalDevice.hpp"
#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult PhysicalDevice::Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks)
	{
		VkPhysicalDeviceProperties physicalDeviceProperties = {
			.apiVersion = VK_API_VERSION_1_4,
			.driverVersion = VK_MAKE_VERSION(0, 1, 0),
			.vendorID = 0x0601,
			.deviceID = 0x060103,
			.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU,
			.deviceName = {},
			.pipelineCacheUUID = {},
			.limits = {},
			.sparseProperties = {},
		};

		using namespace std::string_view_literals;
		constexpr std::string_view deviceName = "Vkd software device"sv;
		std::memcpy(physicalDeviceProperties.deviceName, deviceName.data(), deviceName.size());

		std::array queueFamilyProperties = {
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1}
			},
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_GRAPHICS_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1},
			},
			VkQueueFamilyProperties
			{
				.queueFlags = VK_QUEUE_TRANSFER_BIT,
				.queueCount = 1,
				.timestampValidBits = 0,
				.minImageTransferGranularity = {1, 1, 1},
			}
		};

		return vkd::PhysicalDevice::Create(owner, std::move(physicalDeviceProperties), std::move(queueFamilyProperties), allocationCallbacks);
	}

	DispatchableObjectResult<vkd::Device> PhysicalDevice::CreateDevice()
	{
		DispatchableObject<SoftwareDevice>* softwareDevice = mem::NewDispatchable<SoftwareDevice>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!softwareDevice)
		{
			CCT_ASSERT_FALSE("Could not allocate new SoftwareDevice");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<Device>*>(softwareDevice);
	}
}

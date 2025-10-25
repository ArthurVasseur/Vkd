//
// Created by arthur on 25/10/2025.
//

#include "Vkd/SoftwarePhysicalDevice/SoftwarePhysicalDevice.hpp"
#include "Vkd/SoftwareDevice/SoftwareDevice.hpp"

namespace vkd
{
	VkResult SoftwarePhysicalDevice::Create()
	{
		VkPhysicalDeviceProperties physicalDeviceProperties = {
			.apiVersion = VK_API_VERSION_1_4,
			.driverVersion = VK_MAKE_VERSION(0, 1, 0),
			.vendorID = 0x0601,
			.deviceID = 0x060103,
			.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU,
			.deviceName = "Vkd software device",
			.pipelineCacheUUID = {},
			.limits = {},
			.sparseProperties = {},
		};

		SetPhysicalDeviceProperties(physicalDeviceProperties);

		return PhysicalDevice::Create();
	}

	DispatchableObject<Device>* SoftwarePhysicalDevice::CreateDevice()
	{
		DispatchableObject<SoftwareDevice>* softwareDevice = mem::NewDispatchable<SoftwareDevice>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!softwareDevice)
		{
			CCT_ASSERT_FALSE("Could not allocate new SoftwareDevice");
			return nullptr;
		}
		return reinterpret_cast<DispatchableObject<Device>*>(softwareDevice);
	}
}

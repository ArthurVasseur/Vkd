//
// Created by arthur on 23/04/2025.
//

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	Device::Device() :
		ObjectBase(ObjectType),
		m_owner(nullptr)
	{
	}

	VkResult Device::CreateDevice(VkPhysicalDevice pPhysicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		if (!pAllocator)
			pAllocator = physicalDevice->GetAllocationCallbacks();

		auto* device = physicalDevice->CreateDevice();
		if (!device)
			return VK_ERROR_OUT_OF_HOST_MEMORY;

		device->Object->SetAllocationCallbacks(pAllocator);

		*pDevice = VKD_TO_HANDLE(VkDevice, device);
		return VK_SUCCESS;
	}

	void Device::DestroyDevice(VkDevice pDevice, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);
	}

	PFN_vkVoidFunction Device::GetDeviceProcAddr(VkDevice pDevice, const char* pName)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);

		if (pName == nullptr)
			return nullptr;

#define VKD_ENTRYPOINT_LOOKUP(klass, name)	\
	if (strcmp(pName, "vk" #name) == 0) \
		return (PFN_vkVoidFunction)static_cast<PFN_vk##name>(klass::name)

		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyDevice);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateDevice);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceProcAddr);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceQueue);
#undef VKD_ENTRYPOINT_LOOKUP

		return nullptr;
	}

	void Device::GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
	{
		CCT_ASSERT_FALSE("Not Implemented");
	}
}

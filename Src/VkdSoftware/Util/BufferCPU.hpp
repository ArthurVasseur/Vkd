//
// Created by arthur on 26/10/2025.
//

#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

#include "Vkd/Defines.hpp"
#include "Vkd/Buffer/Buffer.hpp"
#include "VkdSoftware/Memory/DeviceMemory.hpp"

namespace vkd::software
{
	inline uint8_t* GetCPUAddressFromBuffer(VkBuffer handle)
	{
		VKD_FROM_HANDLE(vkd::Buffer, buffer, handle);
		if (!buffer)
			return nullptr;

		if (!buffer->IsBound())
			return nullptr;

		vkd::DeviceMemory* memory = buffer->GetMemory();
		if (!memory)
			return nullptr;

		auto* softwareMemory = static_cast<vkd::software::DeviceMemory*>(memory);
		if (!softwareMemory)
			return nullptr;

		return softwareMemory->Data() + buffer->GetMemoryOffset();
	}
}

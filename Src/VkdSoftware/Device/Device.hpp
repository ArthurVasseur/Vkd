//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "Vkd/Device/Device.hpp"

namespace vkd::software
{
	class SoftwareDevice : public Device
	{
	public:
		SoftwareDevice() = default;

	protected:
		DispatchableObjectResult<vkd::Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) override;
	};
}

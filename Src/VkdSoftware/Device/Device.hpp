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
		Result<vkd::CommandPool*, VkResult> CreateCommandPool() override;
		Result<vkd::Fence*, VkResult> CreateFence() override;
		Result<vkd::Buffer*, VkResult> CreateBuffer() override;
		Result<vkd::DeviceMemory*, VkResult> CreateDeviceMemory() override;
		Result<vkd::Pipeline*, VkResult> CreatePipeline() override;
	};
}

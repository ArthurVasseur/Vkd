//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd::software
{
	class PhysicalDevice : public vkd::PhysicalDevice
	{
	public:
		PhysicalDevice() = default;
		~PhysicalDevice() override = default;

		VkResult Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks) override;

		DispatchableObjectResult<vkd::Device> CreateDevice() override;
	};
}
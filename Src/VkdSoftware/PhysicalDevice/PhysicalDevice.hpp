/**
 * @file PhysicalDevice.hpp
 * @brief Software renderer physical device implementation
 * @date 2025-10-25
 *
 * CPU-based software rendering physical device implementation.
 */

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
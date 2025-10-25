//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	class SoftwarePhysicalDevice : public PhysicalDevice
	{
	public:
		SoftwarePhysicalDevice() = default;
		~SoftwarePhysicalDevice() override = default;

		VkResult Create() override;
		DispatchableObject<Device>* CreateDevice() override;
	};
}
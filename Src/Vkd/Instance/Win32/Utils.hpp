//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "Vkd/Defines.hpp"

#ifdef CCT_PLATFORM_WINDOWS

// STL
#include <vector>

// Libraries
#include <Concerto/Core/Result.hpp>

// Project
#include "Vkd/Instance/Instance.hpp"
#include "Vkd/PhysicalDevice/Wddm/WddmPhysicalDevice.hpp"

namespace vkd::utils
{
	struct WddmAdapterLuid
	{
		cct::UInt32 low;
		cct::UInt32 high;
	};

	static_assert(sizeof(WddmAdapterLuid) == VK_LUID_SIZE, "This is a LUID and they need to match");

	struct WddmAdapterInfo
	{
		WddmAdapterLuid luid;
		cct::UInt32 physicalAdapterIndex;

		struct {
			cct::UInt16 vendorId;
			cct::UInt16 deviceId;
			cct::UInt16 subvendorId;
			cct::UInt16 subdeviceId;
			cct::UInt8 revisionId;
		} device;

		struct {
			cct::UInt8 bus;
			cct::UInt8 dev;
			cct::UInt8 func;
		} bus;
	};
	struct PciInfo
	{
		cct::UInt32 domain;
		cct::UInt32 bus;
		cct::UInt32 dev;
		cct::UInt32 func;
		bool valid;
	};

	cct::Result<std::vector<DispatchableObject<PhysicalDevice>*>, VkResult> EnumerateWddmPhysicalDevices(Instance& instance);
	cct::Result<DispatchableObject<WddmPhysicalDevice>*, VkResult> TryCreatePhysicalDevice(Instance& instance, const WddmAdapterInfo& info);
}

#endif
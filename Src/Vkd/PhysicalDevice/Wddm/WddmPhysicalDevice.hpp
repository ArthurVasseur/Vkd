//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "Vkd/Defines.hpp"
#include "Vkd/Instance/Instance.hpp"

#ifdef CCT_PLATFORM_WINDOWS

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	class WddmPhysicalDevice : public PhysicalDevice
	{
	public:
		WddmPhysicalDevice() = default;
		void SetLuid(LUID luid);
		LUID GetLuid() const;
	private:
		LUID m_luid;
	};
}

#endif
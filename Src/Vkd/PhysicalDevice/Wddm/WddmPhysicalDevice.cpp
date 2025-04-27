//
// Created by arthur on 23/04/2025.
//

#include "Vkd/PhysicalDevice/Wddm/WddmPhysicalDevice.hpp"

#ifdef CCT_PLATFORM_WINDOWS

namespace vkd
{
	void WddmPhysicalDevice::SetLuid(LUID luid)
	{
		m_luid = luid;
	}

	LUID WddmPhysicalDevice::GetLuid() const
	{
		return m_luid;
	}
}

#endif
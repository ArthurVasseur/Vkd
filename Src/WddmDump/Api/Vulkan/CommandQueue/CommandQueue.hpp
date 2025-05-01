//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "WddmDump/Api/Device.hpp"

namespace wddmDump::vk
{
	class Device;
	class CommandQueue : public wddmDump::CommandQueue
	{
		CommandQueue(Device& device, wddmDump::CommandQueue::Type type);
	};
}
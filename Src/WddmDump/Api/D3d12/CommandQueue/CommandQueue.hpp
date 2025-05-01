//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "WddmDump/Api/CommandQueue.hpp"

namespace wddmDump::d3d12
{
	class Device;
	class CommandQueue : public wddmDump::CommandQueue
	{
	public:
		CommandQueue(d3d12::Device& device, wddmDump::CommandQueue::Type);
	};
}
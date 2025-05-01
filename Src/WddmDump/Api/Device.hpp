//
// Created by arthur on 23/04/2025.
//

#pragma once
#include <memory>

#include "WddmDump/Api/CommandQueue.hpp"

namespace wddmDump
{
	class CommandList;

	class Device
	{
	public:
		Device() = default;
		virtual ~Device() = default;

		virtual std::unique_ptr<CommandQueue> CreateCommandQueue(CommandQueue::Type type) = 0;
	};
}

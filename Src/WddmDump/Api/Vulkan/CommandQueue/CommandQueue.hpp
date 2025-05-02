//
// Created by arthur on 23/04/2025.
//

#pragma once

#include <Concerto/Graphics/RHI/Device.hpp>

#include "WddmDump/Api/Device.hpp"

namespace wddmDump::vk
{
	class CommandQueue : public wddmDump::CommandQueue
	{
	public:
		CommandQueue(cct::gfx::rhi::Device& device, wddmDump::CommandQueue::Type type);
	private:
		std::unique_ptr<cct::gfx::rhi::CommandPool> m_commandPool;
	};
}
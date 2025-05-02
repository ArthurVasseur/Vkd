//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "WddmDump/Api/Device.hpp"
#include "Concerto/Graphics/RHI/Device.hpp"

namespace wddmDump::vk
{
	class Device : public wddmDump::Device
	{
	public:
		Device(std::unique_ptr<cct::gfx::rhi::Device> device);

		std::unique_ptr<wddmDump::CommandQueue> CreateCommandQueue(CommandQueue::Type type) override;
	private:
		std::unique_ptr<cct::gfx::rhi::Device> m_device;
	};
}

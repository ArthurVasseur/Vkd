#include "WddmDump/Api/Vulkan/Device/Device.hpp"

#include "WddmDump/Api/Vulkan/CommandQueue/CommandQueue.hpp"

namespace wddmDump::vk
{
	using namespace cct::gfx;

	Device::Device(std::unique_ptr<rhi::Device> device) :
		m_device(std::move(device))
	{
	}

	std::unique_ptr<wddmDump::CommandQueue> Device::CreateCommandQueue(CommandQueue::Type type)
	{
		return std::make_unique<CommandQueue>(*m_device, type);
	}
}

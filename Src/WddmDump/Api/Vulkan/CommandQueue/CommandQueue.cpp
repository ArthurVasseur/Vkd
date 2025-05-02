#include "WddmDump/Api/Vulkan/CommandQueue/CommandQueue.hpp"
#include "WddmDump/Api/Vulkan/Device/Device.hpp"

namespace wddmDump::vk
{
	using namespace cct::gfx;
	CommandQueue::CommandQueue(cct::gfx::rhi::Device& device, wddmDump::CommandQueue::Type type) :
		m_commandPool()
	{
		rhi::QueueFamily queueFamily = {};

		switch (type)
		{
		case CommandQueue::Type::Compute:
			queueFamily = rhi::QueueFamily::Compute;
			break;
		case CommandQueue::Type::Direct:
			queueFamily = rhi::QueueFamily::Graphics;
			break;
		case CommandQueue::Type::Copy:
			queueFamily = rhi::QueueFamily::Transfer;
			break;
		}
		m_commandPool = device.CreateCommandPool(queueFamily);
	}
}

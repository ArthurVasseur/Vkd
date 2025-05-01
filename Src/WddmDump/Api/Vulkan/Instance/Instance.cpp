#include "WddmDump/Api/Vulkan/Instance/Instance.hpp"
#include "WddmDump/Api/Vulkan/Device/Device.hpp"

namespace wddmDump::vk
{
	std::size_t Instance::GetDeviceCount()
	{
		return 0;
	}

	std::unique_ptr<wddmDump::Device> Instance::CreateDevice(std::size_t index)
	{
		return nullptr;
	}
}

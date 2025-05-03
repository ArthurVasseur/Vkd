#include <Concerto/Core/Math/Vector.hpp>
#include "WddmDump/Api/Vulkan/Instance/Instance.hpp"
#include "WddmDump/Api/Vulkan/Device/Device.hpp"

namespace wddmDump::vk
{
	std::size_t Instance::GetDeviceCount()
	{
		return m_instance.EnumerateDevices().size();
	}

	std::unique_ptr<wddmDump::Device> Instance::CreateDevice(std::size_t index)
	{
		auto& deviceInfo = m_instance.EnumerateDevices()[index];
		cct::Logger::Warning("Creating Device name: '{}', vendor: '{}'", deviceInfo.name, deviceInfo.vendor);
		auto device = m_instance.CreateDevice(index);
		return std::make_unique<vk::Device>(std::move(device));
	}
}

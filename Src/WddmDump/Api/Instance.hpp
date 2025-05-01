//
// Created by arthur on 23/04/2025.
//

#pragma once

#include <memory>

namespace wddmDump
{
	class Device;
	class Instance
	{
	public:
		virtual ~Instance() = default;

		virtual std::size_t GetDeviceCount() = 0;
		virtual std::unique_ptr<wddmDump::Device> CreateDevice(std::size_t index) = 0;
	};

	enum class InstanceType
	{
		D3D12,
		Vulkan
	};

	std::unique_ptr<Instance> CreateInstance(InstanceType type);
}

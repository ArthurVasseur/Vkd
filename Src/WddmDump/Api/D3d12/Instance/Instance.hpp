//
// Created by arthur on 23/04/2025.
//

#pragma once

#include <wrl/client.h>
#include <dxgi1_4.h>
#include <vector>

#include "WddmDump/Api/Instance.hpp"

namespace wddmDump::d3d12
{
	class Instance : public wddmDump::Instance
	{
	public:
		Instance();
		~Instance() override = default;

		std::size_t GetDeviceCount() override;
		std::unique_ptr<wddmDump::Device> CreateDevice(std::size_t index) override;
	private:
		Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
		std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> m_devices;
	};
}

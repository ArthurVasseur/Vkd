//
// Created by arthur on 23/04/2025.
//

#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "WddmDump/Api/Device.hpp"

namespace wddmDump::d3d12
{
	class Device : public wddmDump::Device
	{
	public:
		Device(IDXGIAdapter1& adapter);
		~Device() override = default;

		std::unique_ptr<wddmDump::CommandQueue> CreateCommandQueue(CommandQueue::Type type) override;
		ID3D12Device& Get() const;
	private:
		Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	};
}

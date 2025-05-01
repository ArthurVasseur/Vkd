#include "WddmDump/Api/D3d12/Device/Device.hpp"

#include <Concerto/Core/Logger.hpp>

#include "WddmDump/Api/D3d12/CommandQueue/CommandQueue.hpp"

using namespace Microsoft::WRL;

namespace wddmDump::d3d12
{
	Device::Device(IDXGIAdapter1& adapter)
	{
		D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
	}

	std::unique_ptr<wddmDump::CommandQueue> Device::CreateCommandQueue(CommandQueue::Type type)
	{
		return std::make_unique<CommandQueue>(*this, type);
	}

	ID3D12Device& Device::Get() const
	{
		return *m_device.Get();
	}
}

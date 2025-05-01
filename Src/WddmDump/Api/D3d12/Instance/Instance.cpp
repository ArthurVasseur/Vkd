#include "WddmDump/Api/D3d12/Instance/Instance.hpp"

#include <d3dcommon.h>
#include <Concerto/Core/Assert.hpp>
#include <Concerto/Core/Logger.hpp>

#include "WddmDump/Api/D3d12/Instance/Instance.hpp"
#include "WddmDump/Api/D3d12/Device/Device.hpp"

using namespace Microsoft::WRL;

namespace wddmDump::d3d12
{
	Instance::Instance() :
		m_factory(),
		m_devices()
	{
		UINT dxgiFactoryFlags = 0;
		CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));
	}

	std::size_t Instance::GetDeviceCount()
	{
		if (m_devices.empty() == false)
			return m_devices.size();

		std::size_t deviceCount = 0;
		while (true)
		{
			ComPtr<IDXGIAdapter1> adapter = nullptr;
			if (m_factory->EnumAdapters1(static_cast<UINT>(deviceCount), &adapter) == DXGI_ERROR_NOT_FOUND)
				break;
			++deviceCount;
			m_devices.emplace_back(std::move(adapter));
		}
		return deviceCount;
	}

	std::unique_ptr<wddmDump::Device> Instance::CreateDevice(std::size_t index)
	{
		if (index > m_devices.size())
		{
			CCT_ASSERT_FALSE("Invalid device index");
			return nullptr;
		}

		auto& adapter = m_devices[index];
		return std::make_unique<d3d12::Device>(*adapter.Get());
	}
}

#include "VkdAmd/Wddm/WddmDevice.hpp"

#include <utility>

namespace vkd::wddm
{
	WddmDevice::~WddmDevice()
	{
		Destroy();
	}

	WddmDevice::WddmDevice(WddmDevice&& other) noexcept :
		m_adapter(other.m_adapter),
		m_device(other.m_device),
		m_pagingQueue(std::move(other.m_pagingQueue))
	{
		other.m_adapter = 0;
		other.m_device = 0;
	}

	WddmDevice& WddmDevice::operator=(WddmDevice&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_adapter = other.m_adapter;
			m_device = other.m_device;
			m_pagingQueue = std::move(other.m_pagingQueue);
			other.m_adapter = 0;
			other.m_device = 0;
		}
		return *this;
	}

	bool WddmDevice::Create(D3DKMT_HANDLE adapterHandle)
	{
		D3DKMT_CREATEDEVICE createDevice = {};
		createDevice.hAdapter = adapterHandle;
		if (D3DKMTCreateDevice(&createDevice) != STATUS_SUCCESS)
			return false;

		m_adapter = adapterHandle;
		m_device = createDevice.hDevice;
		m_pagingQueue.Create(m_device);
		return true;
	}

	void WddmDevice::Destroy()
	{
		if (m_device == 0)
			return;

		m_pagingQueue.Destroy();

		D3DKMT_DESTROYDEVICE destroyDevice = {};
		destroyDevice.hDevice = m_device;
		D3DKMTDestroyDevice(&destroyDevice);
		m_device = 0;
		m_adapter = 0;
	}

	NTSTATUS WddmDevice::WaitForIdle() const
	{
		if (m_device == 0)
			return static_cast<NTSTATUS>(0xC000000DL);

		D3DKMT_WAITFORIDLE wait = {};
		wait.hDevice = m_device;
		return D3DKMTWaitForIdle(&wait);
	}
} // namespace vkd::wddm

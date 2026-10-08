#pragma once

#include "VkdAmd/Wddm/WddmPagingQueue.hpp"

namespace vkd::wddm
{
	class WddmDevice
	{
	public:
		WddmDevice() = default;
		~WddmDevice();

		WddmDevice(const WddmDevice&) = delete;
		WddmDevice& operator=(const WddmDevice&) = delete;
		WddmDevice(WddmDevice&& other) noexcept;
		WddmDevice& operator=(WddmDevice&& other) noexcept;

		bool Create(D3DKMT_HANDLE adapterHandle);
		void Destroy();

		[[nodiscard]] bool IsValid() const
		{
			return m_device != 0;
		}
		[[nodiscard]] D3DKMT_HANDLE GetDeviceHandle() const
		{
			return m_device;
		}
		[[nodiscard]] D3DKMT_HANDLE GetAdapterHandle() const
		{
			return m_adapter;
		}
		[[nodiscard]] const WddmPagingQueue& GetPagingQueue() const
		{
			return m_pagingQueue;
		}
		NTSTATUS WaitForIdle() const;

	private:
		D3DKMT_HANDLE m_adapter = 0;
		D3DKMT_HANDLE m_device = 0;
		WddmPagingQueue m_pagingQueue;
	};
} // namespace vkd::wddm

#pragma once

#include "VkdAmd/Wddm/WddmAdapter.hpp"

namespace vkd::wddm
{
	class WddmPagingQueue
	{
	public:
		WddmPagingQueue() = default;
		~WddmPagingQueue();

		WddmPagingQueue(const WddmPagingQueue&) = delete;
		WddmPagingQueue& operator=(const WddmPagingQueue&) = delete;
		WddmPagingQueue(WddmPagingQueue&& other) noexcept;
		WddmPagingQueue& operator=(WddmPagingQueue&& other) noexcept;

		bool Create(D3DKMT_HANDLE device);
		void Destroy();

		[[nodiscard]] bool IsValid() const
		{
			return m_pagingQueue != 0;
		}
		[[nodiscard]] D3DKMT_HANDLE GetHandle() const
		{
			return m_pagingQueue;
		}
		[[nodiscard]] D3DKMT_HANDLE GetSyncObject() const
		{
			return m_syncObject;
		}

	private:
		D3DKMT_HANDLE m_device = 0;
		D3DKMT_HANDLE m_pagingQueue = 0;
		D3DKMT_HANDLE m_syncObject = 0;
	};
} // namespace vkd::wddm

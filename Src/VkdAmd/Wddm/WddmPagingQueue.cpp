#include "VkdAmd/Wddm/WddmPagingQueue.hpp"

namespace vkd::wddm
{
	WddmPagingQueue::~WddmPagingQueue()
	{
		Destroy();
	}

	WddmPagingQueue::WddmPagingQueue(WddmPagingQueue&& other) noexcept :
		m_device(other.m_device),
		m_pagingQueue(other.m_pagingQueue),
		m_syncObject(other.m_syncObject)
	{
		other.m_device = 0;
		other.m_pagingQueue = 0;
		other.m_syncObject = 0;
	}

	WddmPagingQueue& WddmPagingQueue::operator=(WddmPagingQueue&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_device = other.m_device;
			m_pagingQueue = other.m_pagingQueue;
			m_syncObject = other.m_syncObject;
			other.m_device = 0;
			other.m_pagingQueue = 0;
			other.m_syncObject = 0;
		}
		return *this;
	}

	bool WddmPagingQueue::Create(D3DKMT_HANDLE device)
	{
		D3DKMT_CREATEPAGINGQUEUE create = {};
		create.hDevice = device;
		create.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
		create.PhysicalAdapterIndex = 0;

		if (D3DKMTCreatePagingQueue(&create) != STATUS_SUCCESS)
			return false;

		m_device = device;
		m_pagingQueue = create.hPagingQueue;
		m_syncObject = create.hSyncObject;
		return true;
	}

	void WddmPagingQueue::Destroy()
	{
		if (m_pagingQueue == 0)
			return;

		D3DDDI_DESTROYPAGINGQUEUE destroy = {};
		destroy.hPagingQueue = m_pagingQueue;
		D3DKMTDestroyPagingQueue(&destroy);

		m_pagingQueue = 0;
		m_syncObject = 0;
		m_device = 0;
	}
} // namespace vkd::wddm

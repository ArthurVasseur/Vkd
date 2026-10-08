#include "VkdAmd/Wddm/WddmAllocation.hpp"

#include <atomic>
#include <cstdlib>

#include <Concerto/Core/Logger/Logger.hpp>

namespace vkd::wddm
{
	static constexpr uint64_t kWddmPageSize = 65536;

	// Live allocation count/bytes, logged every 4096 creations (leak tracking).
	static std::atomic<int64_t> s_liveAllocations{0};
	static std::atomic<int64_t> s_liveBytes{0};

	// VKD_AMD_LOG_ALLOCATIONS=1: every allocation's GPU VA range, as created and destroyed (to tell
	// what a GPU page fault address belongs to).
	static bool LogAllocations()
	{
		static const bool enabled = []
		{
			char value[8] = {};
			size_t length = 0;
			return getenv_s(&length, value, sizeof(value), "VKD_AMD_LOG_ALLOCATIONS") == 0 && length > 1 && value[0] == '1';
		}();
		return enabled;
	}
	static std::atomic<int64_t> s_createdAllocations{0};

	// Waits for a paging fence with a timeout. A synchronous D3DKMTWaitForSynchronizationObjectFromCpu
	// blocks in the kernel forever when the KMD never completes the paging operation (seen with an
	// allocation whose private data was inconsistent) -- leaving an unkillable process behind.
	static bool WaitPagingFence(D3DKMT_HANDLE device, D3DKMT_HANDLE pagingSync, UINT64 fenceValue)
	{
		static constexpr DWORD kPagingTimeoutMs = 5000;
		HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!event)
			return false;
		D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
		wait.hDevice = device;
		wait.ObjectCount = 1;
		wait.ObjectHandleArray = &pagingSync;
		wait.FenceValueArray = &fenceValue;
		wait.hAsyncEvent = event;
		const bool ok = D3DKMTWaitForSynchronizationObjectFromCpu(&wait) == STATUS_SUCCESS &&
						WaitForSingleObject(event, kPagingTimeoutMs) == WAIT_OBJECT_0;
		CloseHandle(event);
		return ok;
	}

	WddmAllocation::~WddmAllocation()
	{
		Destroy();
	}

	WddmAllocation::WddmAllocation(WddmAllocation&& other) noexcept :
		m_device(other.m_device),
		m_adapter(other.m_adapter),
		m_pagingQueue(other.m_pagingQueue),
		m_resource(other.m_resource),
		m_allocation(other.m_allocation),
		m_size(other.m_size),
		m_gpuVa(other.m_gpuVa),
		m_mappedPtr(other.m_mappedPtr)
	{
		other.m_device = 0;
		other.m_adapter = 0;
		other.m_pagingQueue = 0;
		other.m_resource = 0;
		other.m_allocation = 0;
		other.m_size = 0;
		other.m_gpuVa = 0;
		other.m_mappedPtr = nullptr;
	}

	WddmAllocation& WddmAllocation::operator=(WddmAllocation&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_device = other.m_device;
			m_adapter = other.m_adapter;
			m_pagingQueue = other.m_pagingQueue;
			m_resource = other.m_resource;
			m_allocation = other.m_allocation;
			m_size = other.m_size;
			m_gpuVa = other.m_gpuVa;
			m_mappedPtr = other.m_mappedPtr;
			other.m_device = 0;
			other.m_adapter = 0;
			other.m_pagingQueue = 0;
			other.m_resource = 0;
			other.m_allocation = 0;
			other.m_size = 0;
			other.m_gpuVa = 0;
			other.m_mappedPtr = nullptr;
		}
		return *this;
	}

	// Power-of-two sizes from 64KB up: the only allocation shapes AMDVLK creates (4KB...32MB in
	// wddm-dump [submit-records][re]). A 0x1E0000-byte allocation (800x600 RGBA8) wedged the
	// adapter's paging path until reboot, so stay on proven shapes at the cost of up to 2x memory.
	uint64_t WddmAllocation::MappedSize(uint64_t sizeInBytes)
	{
		uint64_t size = kWddmPageSize;
		while (size < sizeInBytes)
			size <<= 1;
		return size;
	}

	bool WddmAllocation::Create(D3DKMT_HANDLE device, D3DKMT_HANDLE pagingQueueHandle, const AllocationDesc& desc,
								D3DKMT_HANDLE pagingSync)
	{
		const uint64_t alignedSize = MappedSize(desc.sizeInBytes);

		D3DDDI_ALLOCATIONINFO2 allocInfo = {};
		allocInfo.pPrivateDriverData = const_cast<void*>(desc.allocPrivateData);
		allocInfo.PrivateDriverDataSize = desc.allocPrivateDataSize;

		D3DKMT_CREATEALLOCATION createAlloc = {};
		createAlloc.hDevice = device;
		createAlloc.pPrivateDriverData = const_cast<void*>(desc.resourcePrivateData);
		createAlloc.PrivateDriverDataSize = desc.resourcePrivateDataSize;
		createAlloc.NumAllocations = 1;
		createAlloc.pAllocationInfo2 = &allocInfo;
		createAlloc.Flags.Zeroed = 1;

		const NTSTATUS createSt = D3DKMTCreateAllocation2(&createAlloc);
		if (createSt != STATUS_SUCCESS)
		{
			cct::Logger::Error("WddmAllocation: CreateAllocation2 NTSTATUS=0x{:08X} (size=0x{:X})", static_cast<uint32_t>(createSt), alignedSize);
			return false;
		}

		m_device = device;
		m_adapter = desc.adapter;
		m_pagingQueue = pagingQueueHandle;
		m_resource = createAlloc.hResource;
		m_allocation = allocInfo.hAllocation;
		m_size = alignedSize;
		s_liveAllocations.fetch_add(1);
		s_liveBytes.fetch_add(static_cast<int64_t>(alignedSize));
		if ((s_createdAllocations.fetch_add(1) + 1) % 4096 == 0)
			cct::Logger::Info("WddmAllocation: {} created, {} live ({} MB)", s_createdAllocations.load(), s_liveAllocations.load(), s_liveBytes.load() >> 20);

		if (pagingQueueHandle != 0)
		{
			static constexpr uint64_t kPageSize4KB = 4096;
			const uint64_t sizeInPages = (alignedSize + kPageSize4KB - 1) / kPageSize4KB;

			// Real AMDVLK never calls D3DKMTReserveGpuVirtualAddress -- it does a single
			// D3DKMTMapGpuVirtualAddress with BaseAddress=0 and a Minimum/Maximum hint range,
			// letting the KMD pick the VA itself (confirmed live: real captures show
			// Min=0x200000000, Max=0x7ffc00000000). Also always Map before MakeResident
			// (confirmed via a 585-call sequence diff against a real capture) -- this was
			// previously backwards here.
			D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE protection = {};
			protection.Write = 1;

			D3DDDI_MAPGPUVIRTUALADDRESS mapVa = {};
			mapVa.hPagingQueue = pagingQueueHandle;
			mapVa.hAllocation = m_allocation;
			mapVa.OffsetInPages = 0;
			mapVa.SizeInPages = sizeInPages;
			mapVa.Protection = protection;
			mapVa.BaseAddress = 0;
			mapVa.MinimumAddress = desc.minimumVa ? desc.minimumVa : 0x200000000ull;
			mapVa.MaximumAddress = desc.maximumVa ? desc.maximumVa : 0x7ffc00000000ull;

			const NTSTATUS mapSt = D3DKMTMapGpuVirtualAddress(&mapVa);
			if (pagingSync != 0 && mapVa.PagingFenceValue > 0 && !WaitPagingFence(device, pagingSync, mapVa.PagingFenceValue))
			{
				cct::Logger::Error("WddmAllocation: MapGpuVirtualAddress paging fence never signaled (size=0x{:X})", alignedSize);
				Destroy();
				return false;
			}

			D3DDDI_MAKERESIDENT makeResident = {};
			makeResident.hPagingQueue = pagingQueueHandle;
			makeResident.NumAllocations = 1;
			const D3DKMT_HANDLE allocHandle = m_allocation;
			makeResident.AllocationList = &allocHandle;
			makeResident.Flags.MustSucceed = 1;
			const NTSTATUS residentSt = D3DKMTMakeResident(&makeResident);
			if (residentSt != STATUS_SUCCESS && residentSt != STATUS_PENDING)
				cct::Logger::Error("WddmAllocation: MakeResident NTSTATUS=0x{:08X} (size=0x{:X})", static_cast<uint32_t>(residentSt), alignedSize);

			if (pagingSync != 0 && makeResident.PagingFenceValue > 0 &&
				!WaitPagingFence(device, pagingSync, makeResident.PagingFenceValue))
			{
				cct::Logger::Error("WddmAllocation: MakeResident paging fence never signaled (size=0x{:X})", alignedSize);
				Destroy();
				return false;
			}

			if ((mapSt == STATUS_SUCCESS || mapSt == STATUS_PENDING) && mapVa.VirtualAddress != 0)
				m_gpuVa = mapVa.VirtualAddress;
			else
				cct::Logger::Error("WddmAllocation: MapGpuVirtualAddress NTSTATUS=0x{:08X} (size=0x{:X})", static_cast<uint32_t>(mapSt), alignedSize);
		}
		if (LogAllocations())
			cct::Logger::Info("WddmAllocation: create [0x{:X}, 0x{:X}) hAllocation=0x{:X}", static_cast<uint64_t>(m_gpuVa),
							  static_cast<uint64_t>(m_gpuVa + m_size), m_allocation);

		return true;
	}

	void WddmAllocation::Destroy()
	{
		if (m_allocation == 0)
			return;

		if (m_mappedPtr)
			UnmapFromHost();
		if (LogAllocations())
			cct::Logger::Info("WddmAllocation: destroy [0x{:X}, 0x{:X}) hAllocation=0x{:X}", static_cast<uint64_t>(m_gpuVa),
							  static_cast<uint64_t>(m_gpuVa + m_size), m_allocation);

		// Same teardown as RADV's wddm2 winsys: evict, free the VA range, destroy. Destroying the
		// allocation alone leaves its VA range reserved: a long CTS run (~150K allocations) consumed
		// ~50GB of VA that way, until the device dropped into an error state between two submits.
		if (m_pagingQueue != 0)
		{
			D3DKMT_EVICT evict = {};
			evict.hDevice = m_device;
			evict.NumAllocations = 1;
			evict.AllocationList = &m_allocation;
			D3DKMTEvict(&evict);
		}
		if (m_gpuVa != 0 && m_adapter != 0)
		{
			D3DKMT_FREEGPUVIRTUALADDRESS freeVa = {};
			freeVa.hAdapter = m_adapter;
			freeVa.BaseAddress = m_gpuVa;
			freeVa.Size = m_size;
			D3DKMTFreeGpuVirtualAddress(&freeVa);
		}

		// By its handle, as RADV does: Create doesn't set CreateResource, so hResource is 0, and
		// {hResource 0, no allocation list} succeeds without destroying anything -- every allocation
		// leaked in the kernel (evicted, its VA freed) until VidMm refused new ones with
		// STATUS_GRAPHICS_NO_VIDEO_MEMORY, ~330K allocations into a CTS run.
		D3DKMT_DESTROYALLOCATION2 destroyAlloc = {};
		destroyAlloc.hDevice = m_device;
		destroyAlloc.hResource = m_resource;
		if (m_resource == 0)
		{
			destroyAlloc.AllocationCount = 1;
			destroyAlloc.phAllocationList = &m_allocation;
		}
		const NTSTATUS destroySt = D3DKMTDestroyAllocation2(&destroyAlloc);
		if (destroySt != STATUS_SUCCESS)
			cct::Logger::Error("WddmAllocation: DestroyAllocation2 NTSTATUS=0x{:08X} (hAllocation=0x{:X})", static_cast<uint32_t>(destroySt), m_allocation);
		s_liveAllocations.fetch_sub(1);
		s_liveBytes.fetch_sub(static_cast<int64_t>(m_size));

		m_allocation = 0;
		m_resource = 0;
		m_device = 0;
		m_adapter = 0;
		m_pagingQueue = 0;
		m_size = 0;
		m_gpuVa = 0;
	}

	void* WddmAllocation::MapToHost()
	{
		if (m_mappedPtr)
			return m_mappedPtr;

		D3DKMT_LOCK2 lock2 = {};
		lock2.hDevice = m_device;
		lock2.hAllocation = m_allocation;
		if (D3DKMTLock2(&lock2) != STATUS_SUCCESS)
			return nullptr;

		m_mappedPtr = lock2.pData;
		return m_mappedPtr;
	}

	void WddmAllocation::UnmapFromHost()
	{
		if (!m_mappedPtr)
			return;

		D3DKMT_UNLOCK2 unlock2 = {};
		unlock2.hDevice = m_device;
		unlock2.hAllocation = m_allocation;
		D3DKMTUnlock2(&unlock2);
		m_mappedPtr = nullptr;
	}
} // namespace vkd::wddm

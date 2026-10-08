#pragma once

#include "VkdAmd/Wddm/WddmAdapter.hpp"

namespace vkd::wddm
{
	struct AllocationDesc
	{
		uint64_t sizeInBytes = 0;
		uint32_t alignment = 0;
		const void* resourcePrivateData = nullptr;
		uint32_t resourcePrivateDataSize = 0;
		const void* allocPrivateData = nullptr;
		uint32_t allocPrivateDataSize = 0;
		// Adapter owning the GPU VA space; needed to free the VA range on Destroy().
		D3DKMT_HANDLE adapter = 0;
		// GPU VA range the KMD picks the mapping from (0 = the default [8GB, 128TB)).
		uint64_t minimumVa = 0;
		uint64_t maximumVa = 0;
	};

	class WddmAllocation
	{
	public:
		WddmAllocation() = default;
		~WddmAllocation();

		WddmAllocation(const WddmAllocation&) = delete;
		WddmAllocation& operator=(const WddmAllocation&) = delete;
		WddmAllocation(WddmAllocation&& other) noexcept;
		WddmAllocation& operator=(WddmAllocation&& other) noexcept;

		bool Create(D3DKMT_HANDLE device, D3DKMT_HANDLE pagingQueueHandle, const AllocationDesc& desc,
					D3DKMT_HANDLE pagingSync = 0);
		void Destroy();

		// Size Create() allocates and maps for a requested size. The allocation private data must
		// declare exactly this size, or the KMD and the GPU VA mapping disagree on the allocation.
		[[nodiscard]] static uint64_t MappedSize(uint64_t sizeInBytes);

		void* MapToHost();
		void UnmapFromHost();

		[[nodiscard]] bool IsValid() const
		{
			return m_allocation != 0;
		}
		[[nodiscard]] D3DKMT_HANDLE GetHandle() const
		{
			return m_allocation;
		}
		[[nodiscard]] uint64_t GetSize() const
		{
			return m_size;
		}
		[[nodiscard]] D3DGPU_VIRTUAL_ADDRESS GetGpuVa() const
		{
			return m_gpuVa;
		}

	private:
		D3DKMT_HANDLE m_device = 0;
		D3DKMT_HANDLE m_adapter = 0;
		D3DKMT_HANDLE m_pagingQueue = 0;
		D3DKMT_HANDLE m_resource = 0;
		D3DKMT_HANDLE m_allocation = 0;
		uint64_t m_size = 0;
		D3DGPU_VIRTUAL_ADDRESS m_gpuVa = 0;
		void* m_mappedPtr = nullptr;
	};
} // namespace vkd::wddm

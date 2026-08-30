/**
 * @file QueryPool.cpp
 * @brief Implementation of Vulkan query pool
 * @date 2026-08-30
 */

#include "Vkd/QueryPool/QueryPool.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace vkd
{
	VkResult QueryPool::Create(Device& owner, const VkQueryPoolCreateInfo& createInfo)
	{
		m_owner = &owner;
		m_queryType = createInfo.queryType;
		m_queryCount = createInfo.queryCount;
		m_pipelineStatistics = createInfo.pipelineStatistics;

		const UInt32 valuesPerQuery = ValuesPerQuery();
		m_results.assign(static_cast<std::size_t>(m_queryCount) * valuesPerQuery, 0);
		m_available.assign(m_queryCount, 0);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	UInt32 QueryPool::ValuesPerQuery() const
	{
		if (m_queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
			return std::max(1u, static_cast<UInt32>(std::popcount(m_pipelineStatistics)));
		return 1;
	}

	VkResult QueryPool::GetResults(UInt32 firstQuery, UInt32 queryCount, std::size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_CHECK(pData);
		VKD_CHECK(firstQuery + queryCount <= m_queryCount);

		const bool want64 = (flags & VK_QUERY_RESULT_64_BIT) != 0;
		const bool wantAvailability = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
		const bool wait = (flags & VK_QUERY_RESULT_WAIT_BIT) != 0;
		const bool partial = (flags & VK_QUERY_RESULT_PARTIAL_BIT) != 0;
		const UInt32 valuesPerQuery = ValuesPerQuery();

		std::unique_lock lock(m_mutex);
		if (wait)
		{
			m_cv.wait(lock, [this, firstQuery, queryCount]
					  {
				for (UInt32 i = 0; i < queryCount; ++i)
				{
					if (m_available[firstQuery + i] == 0)
						return false;
				}
				return true; });
		}

		bool allAvailable = true;
		for (UInt32 i = 0; i < queryCount; ++i)
		{
			if (m_available[firstQuery + i] == 0)
			{
				allAvailable = false;
				break;
			}
		}

		if (!allAvailable && !partial)
			return VK_NOT_READY;

		auto* out = static_cast<UInt8*>(pData);
		for (UInt32 i = 0; i < queryCount; ++i)
		{
			UInt8* record = out + static_cast<std::size_t>(i) * static_cast<std::size_t>(stride);
			const UInt32 query = firstQuery + i;
			const bool available = m_available[query] != 0;
			for (UInt32 valueIndex = 0; valueIndex < valuesPerQuery; ++valueIndex)
			{
				const UInt64 value = (available || partial) ? m_results[static_cast<std::size_t>(query) * valuesPerQuery + valueIndex] : 0;
				if (want64)
				{
					std::memcpy(record, &value, sizeof(UInt64));
					record += sizeof(UInt64);
				}
				else
				{
					const UInt32 narrow = static_cast<UInt32>(value);
					std::memcpy(record, &narrow, sizeof(UInt32));
					record += sizeof(UInt32);
				}
			}

			if (wantAvailability)
			{
				if (want64)
				{
					const UInt64 availability = available ? 1 : 0;
					std::memcpy(record, &availability, sizeof(UInt64));
				}
				else
				{
					const UInt32 availability = available ? 1 : 0;
					std::memcpy(record, &availability, sizeof(UInt32));
				}
			}
		}

		(void)dataSize;
		return allAvailable ? VK_SUCCESS : VK_NOT_READY;
	}
} // namespace vkd

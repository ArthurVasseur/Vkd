/**
 * @file QueryPool.hpp
 * @brief Vulkan query pool
 * @date 2026-08-30
 */

#pragma once

#include <condition_variable>
#include <mutex>

#include "Vkd/Memory/Stl.hpp"
#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class QueryPool : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_QUERY_POOL;
		VKD_NON_DISPATCHABLE_HANDLE(QueryPool);

		explicit QueryPool(const VkAllocationCallbacks& allocationCallbacks);
		~QueryPool() override = default;

		virtual VkResult Create(Device& owner, const VkQueryPoolCreateInfo& createInfo);

		[[nodiscard]] inline Device* GetOwner() const;
		[[nodiscard]] inline VkQueryType GetQueryType() const;
		[[nodiscard]] inline UInt32 GetQueryCount() const;

		VkResult GetResults(UInt32 firstQuery, UInt32 queryCount, std::size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags);

	private:
		[[nodiscard]] UInt32 ValuesPerQuery() const;

		Device* m_owner;
		VkQueryType m_queryType;
		UInt32 m_queryCount;
		VkQueryPipelineStatisticFlags m_pipelineStatistics;
		vkd::vector<UInt64> m_results;
		vkd::vector<UInt8> m_available;
		std::mutex m_mutex;
		std::condition_variable m_cv;
	};
} // namespace vkd

#include "Vkd/QueryPool/QueryPool.inl"

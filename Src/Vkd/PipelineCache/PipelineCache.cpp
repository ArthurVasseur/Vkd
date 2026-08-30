/**
 * @file PipelineCache.cpp
 * @brief Implementation of Vulkan pipeline cache
 * @date 2026-08-30
 */

#include "Vkd/PipelineCache/PipelineCache.hpp"

#include <cstring>

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	VkResult PipelineCache::Create(Device& owner, const VkPipelineCacheCreateInfo& createInfo)
	{
		m_owner = &owner;

		const VkPhysicalDeviceProperties& properties = owner.GetOwner()->GetPhysicalDeviceProperties();
		m_header.headerSize = sizeof(VkPipelineCacheHeaderVersionOne);
		m_header.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
		m_header.vendorID = properties.vendorID;
		m_header.deviceID = properties.deviceID;
		std::memcpy(m_header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE);

		(void)createInfo;
		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	VkResult PipelineCache::GetData(std::size_t* pDataSize, void* pData) const
	{
		VKD_AUTO_PROFILER_SCOPE();
		VKD_CHECK(pDataSize);

		const std::size_t required = sizeof(VkPipelineCacheHeaderVersionOne);
		if (!pData)
		{
			*pDataSize = required;
			return VK_SUCCESS;
		}

		if (*pDataSize < required)
		{
			*pDataSize = required;
			return VK_INCOMPLETE;
		}

		std::memcpy(pData, &m_header, required);
		*pDataSize = required;
		return VK_SUCCESS;
	}

	VkResult PipelineCache::Merge(uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches)
	{
		VKD_AUTO_PROFILER_SCOPE();
		(void)srcCacheCount;
		(void)pSrcCaches;
		return VK_SUCCESS;
	}
} // namespace vkd

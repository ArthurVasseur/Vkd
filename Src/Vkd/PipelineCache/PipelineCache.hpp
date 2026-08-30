/**
 * @file PipelineCache.hpp
 * @brief Vulkan pipeline cache
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/ObjectBase/ObjectBase.hpp"

#include <vulkan/vulkan.h>

namespace vkd
{
	class Device;

	class PipelineCache : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_PIPELINE_CACHE;
		VKD_NON_DISPATCHABLE_HANDLE(PipelineCache);

		explicit PipelineCache(const VkAllocationCallbacks& allocationCallbacks);
		~PipelineCache() override = default;

		virtual VkResult Create(Device& owner, const VkPipelineCacheCreateInfo& createInfo);

		[[nodiscard]] inline Device* GetOwner() const;

		VkResult GetData(std::size_t* pDataSize, void* pData) const;
		VkResult Merge(uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches);

	private:
		Device* m_owner;
		VkPipelineCacheHeaderVersionOne m_header;
	};
} // namespace vkd

#include "Vkd/PipelineCache/PipelineCache.inl"

#pragma once

#include <vulkan/vulkan.h>

#include "Vkd/Buffer/Buffer.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "VkdSoftware/CpuContext/CpuContext.hpp"

namespace vkd::software
{
	class CommandDispatcher
	{
	public:
		explicit CommandDispatcher(CpuContext& ctx);
		~CommandDispatcher() = default;

		VkResult Execute(const CommandBuffer& cb);

	private:
		VkResult operator()(const vkd::Buffer::OpFill& op);
		VkResult operator()(const vkd::Buffer::OpCopy& op);

		CpuContext& m_context;
	};
}

#include "VkdSoftware/CommandDispatcher/CommandDispatcher.inl"

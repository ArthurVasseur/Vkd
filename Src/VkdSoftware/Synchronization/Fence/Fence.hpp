//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/Synchronization/Fence/Fence.hpp"

namespace vkd::software
{
	class Fence : public vkd::Fence
	{
	public:
		Fence() = default;
		~Fence() override = default;

	protected:
		VkResult GetStatus() override;
		VkResult Wait(uint64_t timeout) override;
		VkResult Reset() override;

	private:
		// TODO: implement CPU synchronization primitives
	};
}

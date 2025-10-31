/**
 * @file System.hpp
 * @brief System information utilities for hardware resource queries
 * @date 2025-10-30
 *
 * Provides cross-platform system information retrieval, including RAM queries
 * and device memory heap size calculations for Vulkan driver memory management.
 */

#pragma once

#include <optional>
#include <Concerto/Core/Types/Types.hpp>

namespace vkd::utils
{
	using namespace cct;
	class System
	{
	public:
		System();
		~System() = default;

		UInt64 GetTotalRamBytes();
		std::optional<UInt64> GetAvailableRamBytes();

		void InvalidateCache();

		static UInt64 ComputeDeviceMemoryHeapSize(UInt64 totalRam) noexcept;

	private:
		std::optional<UInt64> m_totalRamBytes;
		std::optional<UInt64> m_availableRamBytes;
	};
}

/**
 * @file System.cpp
 * @brief Implementation of system information utilities
 * @date 2025-10-30
 */

#include "VkdUtils/System/System.hpp"

#if defined(CCT_PLATFORM_WINDOWS)
	#define NOMINMAX
	#include <windows.h>
#elif defined(CCT_PLATFORM_LINUX)
	#include <sys/sysinfo.h>
#elif defined(CCT_PLATFORM_FREEBSD) || defined(CCT_PLATFORM_MACOS)
	#include <sys/types.h>
	#include <sys/sysctl.h>
#endif

namespace vkd::utils
{
	System::System() = default;

	static UInt64 VkdUtils_Internal_QueryTotalRamBytes()
	{
		#if defined(CCT_PLATFORM_WINDOWS)
			MEMORYSTATUSEX statex;
			statex.dwLength = sizeof(statex);
			if (!GlobalMemoryStatusEx(&statex))
			{
				return 0ull;
			}
			return static_cast<UInt64>(statex.ullTotalPhys);
		#elif defined(CCT_PLATFORM_LINUX)
			struct sysinfo info;
			if (sysinfo(&info) != 0)
			{
				return 0ull;
			}
			return static_cast<UInt64>(info.totalram) * static_cast<UInt64>(info.mem_unit);
		#elif defined(CCT_PLATFORM_FREEBSD) || defined(CCT_PLATFORM_MACOS)
			UInt64 memBytes = 0;
			size_t size = sizeof(memBytes);
			const char* key =
			#if defined(CCT_PLATFORM_MACOS)
				"hw.memsize";
			#else
				"hw.physmem";
			#endif
			if (sysctlbyname(key, &memBytes, &size, nullptr, 0) != 0)
			{
				return 0ull;
			}
			return memBytes;
		#else
			return 0ull;
		#endif
	}

	static std::optional<UInt64> VkdUtils_Internal_QueryAvailableRamBytes()
	{
		#if defined(CCT_PLATFORM_WINDOWS)
			MEMORYSTATUSEX statex;
			statex.dwLength = sizeof(statex);
			if (!GlobalMemoryStatusEx(&statex))
			{
				return std::nullopt;
			}
			return static_cast<UInt64>(statex.ullAvailPhys);
		#elif defined(CCT_PLATFORM_LINUX)
			struct sysinfo info;
			if (sysinfo(&info) != 0)
			{
				return std::nullopt;
			}
			return static_cast<UInt64>(info.freeram) * static_cast<UInt64>(info.mem_unit);
		#else
			return std::nullopt;
		#endif
	}

	UInt64 System::GetTotalRamBytes()
	{
		if (!m_totalRamBytes.has_value())
		{
			m_totalRamBytes = VkdUtils_Internal_QueryTotalRamBytes();
		}
		return m_totalRamBytes.value_or(0ull);
	}

	std::optional<UInt64> System::GetAvailableRamBytes()
	{
		if (!m_availableRamBytes.has_value())
		{
			m_availableRamBytes = VkdUtils_Internal_QueryAvailableRamBytes();
		}
		return m_availableRamBytes;
	}

	void System::InvalidateCache()
	{
		m_totalRamBytes.reset();
		m_availableRamBytes.reset();
	}

	UInt64 System::ComputeDeviceMemoryHeapSize(UInt64 totalRam) noexcept
	{
		const UInt64 targetSize = static_cast<UInt64>(totalRam * 0.3);

		if (targetSize == 0)
			return 0;

		UInt64 temp = targetSize;
		int msb = 0;
		while (temp > 1)
		{
			temp >>= 1;
			msb++;
		}
		return 1ULL << msb;
	}
}

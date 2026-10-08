#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off: windows.h and winternl.h must come before d3dkmthk.h
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
// clang-format on

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace vkd::wddm
{
	struct AdapterInfo
	{
		LUID luid{};
		D3DKMT_HANDLE handle = 0;
		char name[256]{};
		uint32_t vendorId = 0;
		uint32_t deviceId = 0;
	};

	std::vector<AdapterInfo> EnumerateAdapters();
	std::optional<AdapterInfo> OpenAdapterFromLuid(LUID luid);
	void CloseAdapter(D3DKMT_HANDLE handle);
} // namespace vkd::wddm

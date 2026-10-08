#include "VkdAmd/Wddm/WddmAdapter.hpp"

#include <SetupAPI.h>
#pragma comment(lib, "setupapi.lib")

namespace vkd::wddm
{
	namespace
	{
		static void FillAdapterInfo(AdapterInfo& info)
		{
			D3DKMT_ADAPTERREGISTRYINFO regInfo = {};
			D3DKMT_QUERYADAPTERINFO queryInfo = {};
			queryInfo.hAdapter = info.handle;
			queryInfo.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
			queryInfo.pPrivateDriverData = &regInfo;
			queryInfo.PrivateDriverDataSize = sizeof(regInfo);
			if (D3DKMTQueryAdapterInfo(&queryInfo) == STATUS_SUCCESS)
				WideCharToMultiByte(CP_UTF8, 0, regInfo.AdapterString, -1, info.name, sizeof(info.name), nullptr, nullptr);

			D3DKMT_QUERY_DEVICE_IDS devIds = {};
			D3DKMT_QUERYADAPTERINFO devQuery = {};
			devQuery.hAdapter = info.handle;
			devQuery.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
			devQuery.pPrivateDriverData = &devIds;
			devQuery.PrivateDriverDataSize = sizeof(devIds);
			if (D3DKMTQueryAdapterInfo(&devQuery) == STATUS_SUCCESS)
			{
				info.vendorId = devIds.DeviceIds.VendorID;
				info.deviceId = devIds.DeviceIds.DeviceID;
			}
		}

		// GUID_DISPLAY_DEVICE_ARRIVAL -- the device-interface class the real AMDVLK ICD actually
		// opens its adapter through (confirmed 2026-10-07 via wddm-dump capture against real
		// amdvlk64.dll: D3DKMTOpenAdapterFromDeviceName against "\\?\root#basicrender#0000#{...}",
		// discovered via SetupDiGetClassDevs/SetupDiEnumDeviceInterfaces -- never
		// D3DKMTEnumAdapters2). Not the cause of the still-open HUNG-on-submit issue (confirmed:
		// the node-0 hw-queue context behaves identically either way), but matches real driver
		// behavior more faithfully than the legacy enumeration API.
		static const GUID kGuidDisplayDeviceArrival = {
			0x1ca05180, 0xa699, 0x450a, {0x9a, 0x0c, 0xde, 0x4f, 0xbe, 0x3d, 0xdd, 0x89}};

		static std::vector<AdapterInfo> EnumerateAdaptersByDeviceArrival()
		{
			std::vector<AdapterInfo> result;

			HDEVINFO devInfo = SetupDiGetClassDevsW(&kGuidDisplayDeviceArrival, nullptr, nullptr,
													DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
			if (devInfo == INVALID_HANDLE_VALUE)
				return result;

			SP_DEVICE_INTERFACE_DATA ifData = {};
			ifData.cbSize = sizeof(ifData);
			for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &kGuidDisplayDeviceArrival, i, &ifData); ++i)
			{
				DWORD requiredSize = 0;
				SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
				if (requiredSize == 0)
					continue;
				std::vector<uint8_t> buf(requiredSize);
				auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
				detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
				if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, requiredSize, nullptr, nullptr))
					continue;

				D3DKMT_OPENADAPTERFROMDEVICENAME open = {};
				open.pDeviceName = detail->DevicePath;
				if (D3DKMTOpenAdapterFromDeviceName(&open) != STATUS_SUCCESS)
					continue;

				AdapterInfo info = {};
				info.handle = open.hAdapter;
				info.luid = open.AdapterLuid;
				FillAdapterInfo(info);
				result.push_back(info);
			}
			SetupDiDestroyDeviceInfoList(devInfo);

			return result;
		}

		static std::vector<AdapterInfo> EnumerateAdaptersLegacy()
		{
			std::vector<AdapterInfo> result;

			D3DKMT_ENUMADAPTERS2 enumAdapters = {};
			if (D3DKMTEnumAdapters2(&enumAdapters) != STATUS_SUCCESS || enumAdapters.NumAdapters == 0)
				return result;

			std::vector<D3DKMT_ADAPTERINFO> rawAdapters(enumAdapters.NumAdapters);
			enumAdapters.pAdapters = rawAdapters.data();
			if (D3DKMTEnumAdapters2(&enumAdapters) != STATUS_SUCCESS)
				return result;

			for (UINT i = 0; i < enumAdapters.NumAdapters; ++i)
			{
				AdapterInfo info = {};
				info.luid = rawAdapters[i].AdapterLuid;
				info.handle = rawAdapters[i].hAdapter;
				FillAdapterInfo(info);
				result.push_back(info);
			}

			return result;
		}
	} // namespace

	std::vector<AdapterInfo> EnumerateAdapters()
	{
		if (std::vector<AdapterInfo> result = EnumerateAdaptersByDeviceArrival(); !result.empty())
			return result;

		return EnumerateAdaptersLegacy();
	}

	std::optional<AdapterInfo> OpenAdapterFromLuid(LUID luid)
	{
		D3DKMT_OPENADAPTERFROMLUID openInfo = {};
		openInfo.AdapterLuid = luid;
		if (D3DKMTOpenAdapterFromLuid(&openInfo) != STATUS_SUCCESS)
			return std::nullopt;

		AdapterInfo info = {};
		info.luid = luid;
		info.handle = openInfo.hAdapter;

		D3DKMT_ADAPTERREGISTRYINFO regInfo = {};
		D3DKMT_QUERYADAPTERINFO queryInfo = {};
		queryInfo.hAdapter = info.handle;
		queryInfo.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
		queryInfo.pPrivateDriverData = &regInfo;
		queryInfo.PrivateDriverDataSize = sizeof(regInfo);
		if (D3DKMTQueryAdapterInfo(&queryInfo) == STATUS_SUCCESS)
			WideCharToMultiByte(CP_UTF8, 0, regInfo.AdapterString, -1, info.name, sizeof(info.name), nullptr, nullptr);

		return info;
	}

	void CloseAdapter(D3DKMT_HANDLE handle)
	{
		D3DKMT_CLOSEADAPTER closeInfo = {};
		closeInfo.hAdapter = handle;
		D3DKMTCloseAdapter(&closeInfo);
	}
} // namespace vkd::wddm

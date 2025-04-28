//
// Created by arthur on 23/04/2025.
//

#include "Vkd/Instance/Win32/Utils.hpp"

#ifdef CCT_PLATFORM_WINDOWS

#include <d3dkmthk.h>
#include <array>
#define NT_SUCCESS(v) (v >= 0)

namespace
{
	NTSTATUS QueryAdapterInfo(D3DKMT_HANDLE adapter_h, KMTQUERYADAPTERINFOTYPE info_type, void* info, size_t info_size)
	{
		D3DKMT_QUERYADAPTERINFO adapterInfo = {
		   .hAdapter = adapter_h,
		   .Type = info_type,
		   .pPrivateDriverData = info,
		   .PrivateDriverDataSize = static_cast<UINT>(info_size),
		};
		return D3DKMTQueryAdapterInfo(&adapterInfo);
	}
}
namespace vkd::utils
{
	cct::Result<std::vector<DispatchableObject<PhysicalDevice>*>, VkResult> EnumerateWddmPhysicalDevices(Instance& instance)
	{
		std::vector<DispatchableObject<PhysicalDevice>*> physicalDevices;
		std::array<D3DKMT_ADAPTERINFO, MAX_ENUM_ADAPTERS> adapterInfos;

		D3DKMT_ENUMADAPTERS2 enumAdapters = {
		  .NumAdapters = adapterInfos.size(),
		  .pAdapters = adapterInfos.data(),
		};

		NTSTATUS status = D3DKMTEnumAdapters2(&enumAdapters);
		if (!NT_SUCCESS(status))
			return Error(VK_ERROR_UNKNOWN, "D3DKMTEnumAdapters2 failed");

		if (enumAdapters.NumAdapters == 0)
			return VK_SUCCESS;

		DeferredExit _([&]()
			{
				for (uint32_t i = 0; i < enumAdapters.NumAdapters; i++)
				{
					D3DKMT_CLOSEADAPTER close_adapter = {
					   .hAdapter = adapterInfos[i].hAdapter,
					};
					status = D3DKMTCloseAdapter(&close_adapter);
					CCT_ASSERT(NT_SUCCESS(status), "D3DKMTCloseAdapter failed");
				}
			});

		VkResult result = VK_SUCCESS;
		for (uint32_t i = 0; i < enumAdapters.NumAdapters; i++)
		{
			D3DKMT_PHYSICAL_ADAPTER_COUNT adapterCount = {};
			status = QueryAdapterInfo(adapterInfos[i].hAdapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &adapterCount, sizeof(adapterCount));
			if (!NT_SUCCESS(status))
				return Error(VK_ERROR_UNKNOWN, "Querying D3DKMT_PHYSICAL_ADAPTER_COUNT failed");

			for (uint32_t j = 0; j < adapterCount.Count; j++)
			{
				D3DKMT_QUERY_DEVICE_IDS queryIds = {};
				queryIds.PhysicalAdapterIndex = j;

				status = QueryAdapterInfo(adapterInfos[i].hAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &queryIds, sizeof(queryIds));
				if (!NT_SUCCESS(status))
				{
					return Error(VK_ERROR_UNKNOWN, "Querying D3DKMT_DEVICE_IDS failed");
				}

				if (queryIds.DeviceIds.VendorID == static_cast<std::underlying_type_t<VendorId>>(VendorId::Microsoft))
				{
					cct::Logger::Info("Skipping Microsoft renderer");
					continue;
				}

				CCT_ASSERT(queryIds.DeviceIds.DeviceID <= std::numeric_limits<cct::UInt16>::max(), "Invalid size");
				CCT_ASSERT(queryIds.DeviceIds.VendorID <= std::numeric_limits<cct::UInt16>::max(), "Invalid size");
				CCT_ASSERT(queryIds.DeviceIds.SubVendorID <= std::numeric_limits<cct::UInt16>::max(), "Invalid size");
				CCT_ASSERT(queryIds.DeviceIds.SubSystemID <= std::numeric_limits<cct::UInt16>::max(), "Invalid size");
				CCT_ASSERT(queryIds.DeviceIds.RevisionID <= std::numeric_limits<cct::UInt8>::max(), "Invalid size");

				D3DKMT_ADAPTERADDRESS address = {};
				status = QueryAdapterInfo(adapterInfos[i].hAdapter, KMTQAITYPE_ADAPTERADDRESS, &address, sizeof(address));
				if (!NT_SUCCESS(status))
				{
					return Error(VK_ERROR_UNKNOWN, "Querying D3DKMT_ADAPTERADDRESS failed");
				}

				CCT_ASSERT(address.BusNumber <= std::numeric_limits<cct::UInt8>::max(), "Invalid size");
				CCT_ASSERT(address.DeviceNumber <= std::numeric_limits<cct::UInt8>::max(), "Invalid size");
				CCT_ASSERT(address.FunctionNumber <= std::numeric_limits<cct::UInt8>::max(), "Invalid size");

				WddmAdapterInfo info = {
					.luid = {
						.low = static_cast<cct::UInt32>(adapterInfos[i].AdapterLuid.LowPart),
						.high = static_cast<cct::UInt32>(adapterInfos[i].AdapterLuid.HighPart),
					},
					.physicalAdapterIndex = j,
					.device = {
						.vendorId = static_cast<cct::UInt16>(queryIds.DeviceIds.VendorID),
						.deviceId = static_cast<cct::UInt16>(queryIds.DeviceIds.DeviceID),
						.subvendorId = static_cast<cct::UInt16>(queryIds.DeviceIds.SubVendorID),
						.subdeviceId = static_cast<cct::UInt16>(queryIds.DeviceIds.SubSystemID),
						.revisionId = static_cast<cct::UInt8>(queryIds.DeviceIds.RevisionID),
					},
					.bus = {
						.bus = static_cast<cct::UInt8>(address.BusNumber),
						.dev = static_cast<cct::UInt8>(address.DeviceNumber),
						.func = static_cast<cct::UInt8>(address.FunctionNumber),
				   },
				};

				auto physicalDeviceResult = TryCreatePhysicalDevice(instance, info);
				if (physicalDeviceResult.IsError())
					result = physicalDeviceResult.GetError();
				else
				{
					auto* wddmPhysicalDevice = physicalDeviceResult.GetValue();
					auto* physicalDevice = reinterpret_cast<DispatchableObject<PhysicalDevice>*>(wddmPhysicalDevice);
					physicalDevices.emplace_back(physicalDevice);
				}
				/* Incompatible DRM device, skip. */
				if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
					result = VK_SUCCESS;
					continue;
				}

				/* Error creating the physical device, report the error. */
				if (result != VK_SUCCESS)
					return Error(result, "Failed to create device");
			}
		}

		return physicalDevices;
	}

	cct::Result<DispatchableObject<WddmPhysicalDevice>*, VkResult> TryCreatePhysicalDevice(Instance& instance, const WddmAdapterInfo& adapterInfo)
	{
		D3DKMT_OPENADAPTERFROMLUID openAdapter = {
			  .AdapterLuid = {
				 .LowPart = static_cast<DWORD>(adapterInfo.luid.low),
				 .HighPart = static_cast<LONG>(adapterInfo.luid.high),
			  },
			.hAdapter = 0
		};

		NTSTATUS status = D3DKMTOpenAdapterFromLuid(&openAdapter);
		if (!NT_SUCCESS(status))
			return Error(VK_ERROR_UNKNOWN, "D3DKMTOpenAdapterFromLuid failed");

		D3DKMT_CLOSEADAPTER closeAdapter = {
			.hAdapter = openAdapter.hAdapter,
		};

		DeferredExit _([&]()
		{
			auto status = D3DKMTCloseAdapter(&closeAdapter);
			CCT_ASSERT(NT_SUCCESS(status), "D3DKMTCloseAdapter failed");
		});

		cct::UInt32 pciId = 0;
		cct::UInt32 pciRevId = 0;
		PciInfo pci = {};
		std::string name;
		VkPhysicalDeviceType physicalDeviceType;
		// GPU info
		{
			// PCI device ids
			{
				D3DKMT_QUERY_DEVICE_IDS queryIds = {};
				
				status = QueryAdapterInfo(openAdapter.hAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS , &queryIds, sizeof(queryIds));
				if (!NT_SUCCESS(status))
					return Error(VK_ERROR_UNKNOWN, "D3DKMTOpenAdapterFromLuid failed");
				pciId = queryIds.DeviceIds.DeviceID;
				pciRevId = queryIds.DeviceIds.RevisionID;
			}

			// PCI bus address
			{
				D3DKMT_ADAPTERADDRESS address = {};
				if (NT_SUCCESS(QueryAdapterInfo(openAdapter.hAdapter, KMTQAITYPE_ADAPTERADDRESS, &address, sizeof(address))))
				{
					pci.domain = 0;
					pci.bus = address.BusNumber;
					pci.dev = address.DeviceNumber;
					pci.func = address.FunctionNumber;
					pci.valid = true;
				}
			}

			// Marketing name
			{
				D3DKMT_ADAPTERREGISTRYINFO registry = {};
				status = QueryAdapterInfo(openAdapter.hAdapter, KMTQAITYPE_ADAPTERREGISTRYINFO_RENDER, &registry, sizeof(registry));
				if (!NT_SUCCESS(status))
				{
					//if KMTQAITYPE_ADAPTERREGISTRYINFO_RENDER is not available, try get the driver description renderer
					D3DKMT_DRIVER_DESCRIPTION registry = {};
					status = QueryAdapterInfo(openAdapter.hAdapter, KMTQAITYPE_DRIVER_DESCRIPTION_RENDER, &registry, sizeof(registry));
					if (!NT_SUCCESS(status))
						return Error(VK_ERROR_UNKNOWN, "QueryAdapterInfo failed");
				}

				name = ToUtf8(registry.AdapterString);
			}

			// Adapter type
			{
				D3DKMT_ADAPTERTYPE adapterType = {};
				NTSTATUS status = QueryAdapterInfo(
					openAdapter.hAdapter,
					KMTQAITYPE_ADAPTERTYPE,
					&adapterType,
					sizeof(adapterType)
				);
				if (!NT_SUCCESS(status))
					return Error(VK_ERROR_UNKNOWN, "Querying ADAPTERTYPE failed");

				if (adapterType.HybridIntegrated)
					physicalDeviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
				else if (adapterType.HybridDiscrete)
					physicalDeviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
				else if (adapterType.SoftwareDevice)
					physicalDeviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
				else
					physicalDeviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
			}
		}

		DispatchableObject<WddmPhysicalDevice>* physicalDevice = mem::NewDispatchable<WddmPhysicalDevice>(instance.GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

		if (!physicalDevice)
			return Error(VK_ERROR_OUT_OF_HOST_MEMORY, "Could not allocate WddmPhysicalDevice.");

		VkPhysicalDeviceProperties properties = {
			.apiVersion = VKD_VK_API_VERSION,
			.driverVersion = VKD_DRIVER_VERSION,
			.vendorID = adapterInfo.device.vendorId,
			.deviceID = adapterInfo.device.deviceId,
			.deviceType = physicalDeviceType,
			.deviceName = {},
			.pipelineCacheUUID = {},
			.limits = {},
			.sparseProperties = {}
		};

		std::memcpy(properties.deviceName, name.data(), std::min(sizeof(properties.deviceName), name.size()));

		physicalDevice->Object.SetAllocationCallbacks(instance.GetAllocationCallbacks());
		physicalDevice->Object.SetInstance(instance);
		physicalDevice->Object.SetPhysicalDeviceProperties(properties);
		physicalDevice->Object.SetLuid(LUID{.LowPart = static_cast<DWORD>(adapterInfo.luid.low), .HighPart = static_cast<LONG>(adapterInfo.luid.high)});

		return physicalDevice;
	}
}

#endif
// Direct WDDM probe: companion → paging → 22 init allocs → CreateContextVirtual (AMDVLK's 23
// private escapes are left out: not needed, see Amd/docs, 2026-10-07).
// Mirrors AMDVLK's device/context creation (AmdDevice::Create no longer creates the 22 init
// allocations) without loading the Vulkan ICD.

#include <cstdio>
#include <utility>
#include <vector>

#include "VkdAmd/ContextPrivateData.hpp"
#include "VkdAmd/Pm4.hpp"
#include "VkdAmd/PrivateData.hpp"
#include "VkdAmd/Wddm/WddmAdapter.hpp"
#include "VkdAmd/Wddm/WddmAllocation.hpp"
#include "VkdAmd/Wddm/WddmContext.hpp"
#include "VkdAmd/Wddm/WddmDevice.hpp"

using namespace vkd::amd;
using namespace vkd::wddm;

int main()
{
	std::printf("=== vkd-amd pre-context sequence ===\n");

	D3DKMT_HANDLE companionDevice = 0;
	for (const auto& adapter : EnumerateAdapters())
	{
		if (adapter.vendorId == 0x1002)
			continue;
		D3DKMT_CREATEDEVICE cd = {};
		cd.hAdapter = adapter.handle;
		if (D3DKMTCreateDevice(&cd) == STATUS_SUCCESS)
		{
			companionDevice = cd.hDevice;
			std::printf("1. companion: hDevice=0x%X (%s)\n", companionDevice, adapter.name);
			break;
		}
	}
	if (companionDevice == 0)
		std::printf("1. companion: none\n");

	D3DKMT_HANDLE amdHandle = 0;
	for (const auto& adapter : EnumerateAdapters())
	{
		if (adapter.vendorId == 0x1002)
		{
			amdHandle = adapter.handle;
			std::printf("2. AMD adapter: %s deviceId=0x%X\n", adapter.name, adapter.deviceId);
			break;
		}
	}
	if (amdHandle == 0)
	{
		std::printf("no AMD adapter\n");
		return 1;
	}

	WddmDevice wddm;
	if (!wddm.Create(amdHandle))
	{
		std::printf("D3DKMTCreateDevice failed\n");
		return 1;
	}
	std::printf("2. AMD device: hDevice=0x%X paging=%d\n",
				wddm.GetDeviceHandle(), wddm.GetPagingQueue().IsValid() ? 1 : 0);

	static constexpr uint32_t kInitSurfFlags[] = {
		0x20002004,
		0x20002004,
		0x20002004,
		0x20002004,
		0x2004200C,
		0x2004200C,
		0x20002004,
		0x20002004,
		0x20002004,
		0x20002004,
		0x20002004,
		0x20002004,
		0x20002005,
		0x20002005,
		0x20002005,
		0x20002005,
		0x20002005,
		0x20002005,
		0x20002005,
		0x20002005,
		0x2004200C,
		0x2004200C,
	};

	std::vector<WddmAllocation> allocs;
	ResourcePrivateData resData;
	const D3DKMT_HANDLE pq = wddm.GetPagingQueue().GetHandle();
	for (uint32_t sf : kInitSurfFlags)
	{
		const AllocBlobBuffer blob = MakeAllocBlobInit(sf);
		AllocationDesc desc = {};
		desc.sizeInBytes = 65536;
		desc.alignment = 0x10000;
		desc.resourcePrivateData = &resData;
		desc.resourcePrivateDataSize = sizeof(resData);
		desc.allocPrivateData = blob.ptr();
		desc.allocPrivateDataSize = kAllocBlobSize;
		WddmAllocation alloc;
		if (alloc.Create(wddm.GetDeviceHandle(), pq, desc))
			allocs.push_back(std::move(alloc));
	}
	std::printf("4. init allocs: %zu/%zu\n", allocs.size(), std::size(kInitSurfFlags));

	for (size_t i = 0; i + 1 < allocs.size(); ++i)
	{
		if (allocs[i].MapToHost())
			allocs[i].UnmapFromHost();
	}
	if (!allocs.empty())
		allocs.back().MapToHost();

	const ContextPrivateData priv{};
	int successCount = 0;
	std::vector<WddmContext> kept;
	for (uint32_t node : {0u, 1u, 2u})
	{
		WddmContext ctx;
		const NTSTATUS st = ctx.Create(wddm.GetDeviceHandle(), node, &priv, sizeof(priv));
		std::printf("6. CreateContextVirtual node=%u NTSTATUS=0x%08X%s (kept alive)\n",
					node, static_cast<uint32_t>(st),
					st == STATUS_SUCCESS ? " SUCCESS" : " FAIL");
		if (st == STATUS_SUCCESS)
		{
			++successCount;
			kept.push_back(std::move(ctx));
		}
	}

	// Node 1 failed on the first GPU with HwQueueSupported. Probe flags variants.
	auto tryNode1 = [&](const char* label, UINT flagsValue)
	{
		D3DKMT_CREATECONTEXTVIRTUAL create = {};
		create.hDevice = wddm.GetDeviceHandle();
		create.NodeOrdinal = 1;
		create.EngineAffinity = 1;
		create.ClientHint = D3DKMT_CLIENTHINT_VULKAN;
		create.pPrivateDriverData = const_cast<ContextPrivateData*>(&priv);
		create.PrivateDriverDataSize = sizeof(priv);
		create.Flags.Value = flagsValue;
		const NTSTATUS st = D3DKMTCreateContextVirtual(&create);
		std::printf("6b. node=1 flags=0x%X (%s) NTSTATUS=0x%08X%s\n",
					flagsValue, label, static_cast<uint32_t>(st),
					st == STATUS_SUCCESS ? " SUCCESS" : " FAIL");
		if (st == STATUS_SUCCESS)
		{
			++successCount;
			D3DKMT_DESTROYCONTEXT dc = {};
			dc.hContext = create.hContext;
			D3DKMTDestroyContext(&dc);
		}
	};
	tryNode1("none", 0);
	tryNode1("HwQueueSupported", 0x10);
	tryNode1("DisableGpuTimeout", 0x4);
	tryNode1("both", 0x14);

	// No submit here any more: this probe used to submit PKT2 NOPs with the pre-2026-10-07
	// SubmitPrivateData (records pointing back at the IB, no EOP postamble), which faults the hw
	// queue and can end in a TDR. Real submits need Queue::Submit's IB records + postamble.
	std::printf("7. submit skipped (see Queue::Submit / wddm-dump [ctx-seq][re])\n");

	if (companionDevice != 0)
	{
		D3DKMT_DESTROYDEVICE dd = {};
		dd.hDevice = companionDevice;
		D3DKMTDestroyDevice(&dd);
	}

	std::printf("=== contexts ok: %d ===\n", successCount);
	return successCount > 0 ? 0 : 2;
}

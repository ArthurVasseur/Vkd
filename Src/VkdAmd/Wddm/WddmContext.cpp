#include "VkdAmd/Wddm/WddmContext.hpp"

#include "VkdAmd/HwQueuePrivateData.hpp"

namespace vkd::wddm
{
	WddmContext::~WddmContext()
	{
		Destroy();
	}

	WddmContext::WddmContext(WddmContext&& other) noexcept :
		m_device(other.m_device),
		m_context(other.m_context),
		m_hwQueue(other.m_hwQueue),
		m_hwQueueFence(other.m_hwQueueFence),
		m_submitFenceId(other.m_submitFenceId)
	{
		other.m_device = 0;
		other.m_context = 0;
		other.m_hwQueue = 0;
		other.m_hwQueueFence = 0;
		other.m_submitFenceId = 0;
	}

	WddmContext& WddmContext::operator=(WddmContext&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_device = other.m_device;
			m_context = other.m_context;
			m_hwQueue = other.m_hwQueue;
			m_hwQueueFence = other.m_hwQueueFence;
			m_submitFenceId = other.m_submitFenceId;
			other.m_device = 0;
			other.m_context = 0;
			other.m_hwQueue = 0;
			other.m_hwQueueFence = 0;
			other.m_submitFenceId = 0;
		}
		return *this;
	}

	NTSTATUS WddmContext::Create(D3DKMT_HANDLE device, uint32_t nodeOrdinal,
								 const void* privData, uint32_t privDataSize)
	{
		D3DKMT_CREATECONTEXTVIRTUAL create = {};
		create.hDevice = device;
		create.NodeOrdinal = nodeOrdinal;
		create.EngineAffinity = 1;
		create.ClientHint = D3DKMT_CLIENTHINT_VULKAN;
		create.pPrivateDriverData = const_cast<void*>(privData);
		create.PrivateDriverDataSize = privDataSize;
		create.Flags.HwQueueSupported = 1;

		NTSTATUS status = D3DKMTCreateContextVirtual(&create);
		if (status != STATUS_SUCCESS)
		{
			create.Flags.Value = 0;
			status = D3DKMTCreateContextVirtual(&create);
			if (status != STATUS_SUCCESS)
				return status;
		}

		m_device = device;
		m_context = create.hContext;

		amd::HwQueuePrivateData hwPriv = amd::MakeHwQueuePrivateData(nodeOrdinal);
		D3DKMT_CREATEHWQUEUE hwq = {};
		hwq.hHwContext = m_context;
		hwq.PrivateDriverDataSize = sizeof(hwPriv);
		hwq.pPrivateDriverData = &hwPriv;
		status = D3DKMTCreateHwQueue(&hwq);
		if (status == STATUS_SUCCESS)
		{
			m_hwQueue = hwq.hHwQueue;
			m_hwQueueFence = hwq.hHwQueueProgressFence;
		}

		return STATUS_SUCCESS;
	}

	NTSTATUS WddmContext::Submit(uint64_t commandGpuVa, uint32_t commandLength,
								 const void* privData, uint32_t privDataSize)
	{
		// Hw-queue submits only, and only with the IB records (vkd::amd::MakeSubmitPrivateData):
		// the KMD rejects anything else, and the legacy D3DKMTSubmitCommand path never ran on
		// this hardware.
		if (m_hwQueue == 0 || privData == nullptr || privDataSize == 0)
			return static_cast<NTSTATUS>(0xC000000DL);

		D3DKMT_SUBMITCOMMANDTOHWQUEUE submit = {};
		submit.hHwQueue = m_hwQueue;
		submit.HwQueueProgressFenceId = m_submitFenceId + 1;
		submit.CommandBuffer = commandGpuVa;
		submit.CommandLength = commandLength;
		submit.pPrivateDriverData = const_cast<void*>(privData);
		submit.PrivateDriverDataSize = privDataSize;
		const NTSTATUS st = D3DKMTSubmitCommandToHwQueue(&submit);
		if (st == STATUS_SUCCESS)
			m_submitFenceId = submit.HwQueueProgressFenceId;
		return st;
	}

	NTSTATUS WddmContext::WaitForProgressFence() const
	{
		if (m_device == 0)
			return static_cast<NTSTATUS>(0xC000000DL);

		if (m_hwQueueFence == 0 || m_submitFenceId == 0)
		{
			D3DKMT_WAITFORIDLE wait = {};
			wait.hDevice = m_device;
			return D3DKMTWaitForIdle(&wait);
		}

		D3DKMT_HANDLE fenceHandle = m_hwQueueFence;
		const UINT64 fenceValue = m_submitFenceId;
		D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
		wait.hDevice = m_device;
		wait.ObjectCount = 1;
		wait.ObjectHandleArray = &fenceHandle;
		wait.FenceValueArray = &fenceValue;
		return D3DKMTWaitForSynchronizationObjectFromCpu(&wait);
	}

	void WddmContext::Destroy()
	{
		if (m_hwQueue != 0)
		{
			D3DKMT_DESTROYHWQUEUE destroyQueue = {};
			destroyQueue.hHwQueue = m_hwQueue;
			D3DKMTDestroyHwQueue(&destroyQueue);
			m_hwQueue = 0;
			m_hwQueueFence = 0;
		}

		if (m_context == 0)
			return;

		D3DKMT_DESTROYCONTEXT destroy = {};
		destroy.hContext = m_context;
		D3DKMTDestroyContext(&destroy);
		m_context = 0;
		m_device = 0;
	}
} // namespace vkd::wddm

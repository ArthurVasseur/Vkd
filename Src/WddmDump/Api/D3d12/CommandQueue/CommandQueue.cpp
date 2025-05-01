#include "WddmDump/Api/D3d12/CommandQueue/CommandQueue.hpp"
#include "WddmDump/Api/D3d12/Device/Device.hpp"

#include <d3d12.h>
#include <Concerto/Core/Logger.hpp>
#include <wrl/client.h>

namespace wddmDump::d3d12
{
	CommandQueue::CommandQueue(d3d12::Device& device, wddmDump::CommandQueue::Type type)
	{
		D3D12_COMMAND_LIST_TYPE cmdListType;
		switch (type)
		{
		case Type::Compute:
			cmdListType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			break;
		case Type::Direct:
			cmdListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
			break;
		case Type::Copy:
			cmdListType = D3D12_COMMAND_LIST_TYPE_COPY;
			break;
		}
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue;
		D3D12_COMMAND_QUEUE_DESC desc = {.Type = cmdListType, .Priority = 0, .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .NodeMask = 0};
		device.Get().CreateCommandQueue(&desc, IID_PPV_ARGS(&cmdQueue));
	}
}

#include "WddmDump/Api/Instance.hpp"

#include "WddmDump/Api/D3d12/Instance/Instance.hpp"
#undef min
#undef max
#include "WddmDump/Api/Vulkan/Instance/Instance.hpp"

namespace wddmDump
{
	std::unique_ptr<wddmDump::Instance> CreateInstance(InstanceType type)
	{
		if (type == InstanceType::D3D12)
			return std::make_unique<d3d12::Instance>();
		return std::make_unique<vk::Instance>();
	}
}
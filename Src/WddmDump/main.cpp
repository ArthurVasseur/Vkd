#include <array>
#include <Concerto/Core/Logger.hpp>
#include <Concerto/Core/Assert.hpp>
#include <Windows.h>
#include <d3dkmthk.h>
#include <vector>
#include <detours.h>
#include "WddmDump/WddmFunction.hpp"
#include <vcruntime.h>

#undef min
#undef max
#include "Api/Instance.hpp"
#include "Api/Device.hpp"
#include "Api/CommandQueue.hpp"

template<typename F>
class DeferredExit final
{
public:
	DeferredExit(F&& functor) : m_functor(std::move(functor)) {}
	DeferredExit(DeferredExit&) = delete;

	~DeferredExit()
	{
		m_functor();
	}
private:
	F m_functor;
};

inline std::string ToUtf8(const wchar_t* wstr)
{
	char* oldLocale = std::setlocale(LC_CTYPE, "en_US.utf8");
	DeferredExit _([&]()
		{
			std::setlocale(LC_CTYPE, oldLocale);
		});

	std::mbstate_t state = {};
	std::size_t len = std::wcsrtombs(nullptr, &wstr, 0, &state);
	if (len == std::numeric_limits<std::size_t>::max())
	{
		CCT_ASSERT_FALSE("Invalid size");
		return {};
	}

	std::string name;
	name.resize(len);
	std::wcstombs(name.data(), wstr, name.size());

	return name;
}


//}

int main()
{
	HMODULE hGdi32 = LoadLibraryW(L"gdi32.dll");

	LoadWddmFunctions(hGdi32);
	AttachWddmToDetour();
	DeferredExit _([&]()
		{
			DetachWddmFromDetour();
			FreeLibrary(hGdi32);
		});

	auto instance = wddmDump::CreateInstance(wddmDump::InstanceType::D3D12);

	std::vector<std::unique_ptr<wddmDump::Device>> devices;
	auto deviceCount = instance->GetDeviceCount();

	for (std::size_t i = 0; i < deviceCount; ++i)
	{
		auto device = instance->CreateDevice(i);
		devices.push_back(std::move(device));
	}

	std::unordered_map<wddmDump::Device*, std::vector<std::unique_ptr<wddmDump::CommandQueue>>> commandQueues;

	for (auto& device : devices)
	{
		for (auto& type : { wddmDump::CommandQueue::Type::Compute, wddmDump::CommandQueue::Type::Copy, wddmDump::CommandQueue::Type::Direct })
		{
			auto commandQueue = device->CreateCommandQueue(type);
			auto it = commandQueues.find(device.get());
			if (it == commandQueues.end())
			{
				auto r = commandQueues.emplace(device.get(), std::vector<std::unique_ptr<wddmDump::CommandQueue>>{});
				r.first->second.emplace_back(std::move(commandQueue));
			}
			else
				it->second.emplace_back(std::move(commandQueue));
		}
	}

	return 0;
}
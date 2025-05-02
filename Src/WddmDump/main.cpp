#include <array>
#include <Concerto/Core/Logger.hpp>
#include <Concerto/Core/Assert.hpp>
#include <Concerto/Core/DeferredExit.hpp>
#include <Windows.h>
#include <d3dkmthk.h>
#include <vector>
#include <detours.h>
#include "WddmDump/WddmFunction.hpp"
#include <vcruntime.h>

#undef min
#undef max
#include <fstream>

#include "Api/Instance.hpp"
#include "Api/Device.hpp"
#include "Api/CommandQueue.hpp"

inline std::string ToUtf8(const wchar_t* wstr)
{
	char* oldLocale = std::setlocale(LC_CTYPE, "en_US.utf8");
	cct::DeferredExit _([&]()
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

int main()
{
	HMODULE hGdi32 = LoadLibraryW(L"gdi32.dll");

	LoadWddmFunctions(hGdi32);
	AttachWddmToDetour();
	cct::DeferredExit _([&]()
	{
		DetachWddmFromDetour();
		FreeLibrary(hGdi32);
	});

	auto instance = CreateInstance(wddmDump::InstanceType::D3D12);
	if (instance == nullptr)
		return EXIT_FAILURE;

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

	auto json = GetWddmJson().dump(4);
	std::ofstream jsonFile;
	jsonFile.open("./dump.json");
	jsonFile << json;
	return EXIT_SUCCESS;
}
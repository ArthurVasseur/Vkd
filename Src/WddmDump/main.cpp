#include <array>
#include <Concerto/Core/Logger.hpp>
#include <Concerto/Core/Assert.hpp>
#include <Windows.h>
#include <d3dkmthk.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>
#include <wrl/client.h>
#include <detours.h>
#include "WddmDump/WddmFunction.hpp"

#undef min
#undef max
using namespace Microsoft::WRL;

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

void CreateCommandList(ID3D12Device& device)
{
	std::array commandListType = {
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_LIST_TYPE_COMPUTE,
		D3D12_COMMAND_LIST_TYPE_COPY
	};

	for (auto type : commandListType)
	{
		ComPtr<ID3D12CommandQueue> cmdQueue;
		D3D12_COMMAND_QUEUE_DESC desc = {.Type = type, .Priority = 0, .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE, .NodeMask = 0};
		device.CreateCommandQueue(&desc, IID_PPV_ARGS(&cmdQueue));
	}
}

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
	UINT dxgiFactoryFlags = 0;
	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	std::vector<ComPtr<ID3D12Device>> devices;

	for (UINT adapterIndex = 0; ; ++adapterIndex)
	{
		ComPtr<IDXGIAdapter1> pAdapter = nullptr;
		if (factory->EnumAdapters1(adapterIndex, &pAdapter) == DXGI_ERROR_NOT_FOUND)
			break;

		DXGI_ADAPTER_DESC adapterDesc = {};
		HRESULT result = pAdapter->GetDesc(&adapterDesc);
		if (FAILED(result))
			continue;

		result = D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr);
		if (FAILED(result))
		{
			cct::Logger::Error("Device '{}' does not support D3D12", ToUtf8(adapterDesc.Description));
			continue;
		}

		cct::Logger::Info("Found device: {}", ToUtf8(adapterDesc.Description));
		ComPtr<ID3D12Device> device;
		D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
		devices.emplace_back(std::move(device));
	}

	for (auto& device : devices)
		CreateCommandList(*device.Get());

	return 0;
}
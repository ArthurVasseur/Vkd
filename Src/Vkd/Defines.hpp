/**
 * @file Defines.hpp
 * @brief Common definitions and macros for the Vkd driver
 * @date 2025-04-23
 *
 * Provides Vulkan includes, platform detection, handle macros, and common type definitions
 * used throughout the driver implementation.
 */

#pragma once

#include <Concerto/Core/Types/Types.hpp>
#include <Concerto/Core/Assert.hpp>
#include <Concerto/Core/EnumFlags/EnumFlags.hpp>

#include <NazaraUtils/TypeList.hpp>

#include <vulkan/vulkan_core.h>
#include <vulkan/vk_icd.h>
#include <vulkan/utility/vk_dispatch_table.h>


#ifdef CCT_PLATFORM_WINDOWS
	#include <Windows.h>
	#undef min
	#undef max
	#include <vulkan/vulkan_win32.h>
#endif

#define VKD_EXPORT extern "C" CCT_EXPORT

#define VKD_VK_API_VERSION VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION)
#define VKD_DRIVER_VERSION VK_MAKE_VERSION(0, 1, 0)

#ifdef VKD_DEBUG_CHECKS
#define VKD_CHECK(cond) CCT_ASSERT(cond, #cond " is false / null.")
#else
#define VKD_CHECK(cond) do {} while(false)
#endif

#ifdef VKD_PROFILING
#define TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <source_location>
#define VKD_PROFILER_SCOPE(name) ZoneScopedN(name)
#define VKD_AUTO_PROFILER_SCOPE() ZoneScoped
#else
#define VKD_PROFILER_SCOPE(name)
#define VKD_AUTO_PROFILER_SCOPE()
#endif

#define VKD_FROM_HANDLE(type, name, handle)	\
	VKD_CHECK(handle != nullptr);			\
	type* name = type::FromHandle(handle);	\
	VKD_CHECK((name) != nullptr);			\
	VKD_CHECK(dynamic_cast<type*>(name) != nullptr)

#define VKD_TO_HANDLE(type, handle)	\
	(type)(handle)

#define VKD_DISPATCHABLE_HANDLE(type)															\
		static inline type* FromHandle(Vk##type instance)										\
		{																						\
			auto* dispatchable = reinterpret_cast<DispatchableObject<type>*>(instance);			\
			if (!dispatchable)																	\
				return nullptr;																	\
			if (dispatchable->Object->GetObjectType() != type::ObjectType)						\
			{																					\
				CCT_ASSERT_FALSE("Invalid Object Type for: " #type);							\
				return nullptr;																	\
			}																					\
			return dispatchable->Object;														\
		}

#define VKD_NON_DISPATCHABLE_HANDLE(type)														\
		static inline type* FromHandle(Vk##type instance)										\
		{																						\
			return reinterpret_cast<type*>(instance);											\
		}

namespace vkd
{
	using namespace cct;
	enum class VendorId : UInt32
	{
		Microsoft = 0x1414,
		Amd = 0x1002,//0x1022,
		Nvidia = 0x10DE,
		Qualcomm = 0x17CB,
		Intel = 0x8086
	};


	// TODO: Move this class to ConcertoCore
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

	template<typename... Types>
	VkResult Error(VkResult result, const std::format_string<Types...> fmt, Types&&... args)
	{
		cct::Logger::Error(fmt, std::forward<Types...>(args)...);
		if (cct::IsDebuggerAttached())
			CCT_BREAK_IN_DEBUGGER;
		return result;
	}
}
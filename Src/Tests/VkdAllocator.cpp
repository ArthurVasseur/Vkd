/**
 * @file Test/VkdAllocator.cpp
 * @brief Unit tests for the VkAllocationCallbacks backed STL allocator
 * @date 2026-08-27
 */

#include <cstdlib>
#include <new>

#include <catch2/catch_test_macros.hpp>
#include <Vkd/Memory/Allocator.hpp>
#include <Vkd/Memory/Stl.hpp>

using namespace vkd;

namespace
{
	struct AllocationTracker
	{
		std::size_t allocationCount = 0;
		std::size_t freeCount = 0;
		std::size_t liveBytes = 0;
		std::size_t lastAlignment = 0;
		VkSystemAllocationScope lastScope = VK_SYSTEM_ALLOCATION_SCOPE_COMMAND;
		bool failNextAllocation = false;
	};

	// Every block carries its size so the tracker can balance allocations against frees.
	struct BlockHeader
	{
		std::size_t size;
	};

	constexpr std::size_t HeaderSize = sizeof(BlockHeader) < alignof(std::max_align_t) ? alignof(std::max_align_t) : sizeof(BlockHeader);

	void* VKAPI_PTR TrackedAllocation(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
	{
		auto& tracker = *static_cast<AllocationTracker*>(pUserData);
		if (tracker.failNextAllocation)
			return nullptr;

		void* block = std::malloc(size + HeaderSize);
		if (!block)
			return nullptr;

		static_cast<BlockHeader*>(block)->size = size;

		++tracker.allocationCount;
		tracker.liveBytes += size;
		tracker.lastAlignment = alignment;
		tracker.lastScope = allocationScope;

		return static_cast<UInt8*>(block) + HeaderSize;
	}

	void* VKAPI_PTR TrackedReallocation(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
	{
		return nullptr;
	}

	void VKAPI_PTR TrackedFree(void* pUserData, void* pMemory)
	{
		if (!pMemory)
			return;

		auto& tracker = *static_cast<AllocationTracker*>(pUserData);
		void* block = static_cast<UInt8*>(pMemory) - HeaderSize;

		++tracker.freeCount;
		tracker.liveBytes -= static_cast<BlockHeader*>(block)->size;

		std::free(block);
	}

	VkAllocationCallbacks MakeCallbacks(AllocationTracker& tracker)
	{
		return VkAllocationCallbacks{
			.pUserData = &tracker,
			.pfnAllocation = TrackedAllocation,
			.pfnReallocation = TrackedReallocation,
			.pfnFree = TrackedFree,
			.pfnInternalAllocation = nullptr,
			.pfnInternalFree = nullptr};
	}
} // namespace

TEST_CASE("Vkd allocator - Routes through the callbacks", "[vkd][allocator]")
{
	AllocationTracker tracker;
	VkAllocationCallbacks callbacks = MakeCallbacks(tracker);

	SECTION("allocate and deallocate reach the callbacks")
	{
		allocator<UInt32> alloc(callbacks, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

		UInt32* memory = alloc.allocate(16);
		REQUIRE(memory != nullptr);
		REQUIRE(tracker.allocationCount == 1);
		REQUIRE(tracker.liveBytes == 16 * sizeof(UInt32));
		REQUIRE(tracker.lastAlignment == alignof(UInt32));
		REQUIRE(tracker.lastScope == VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

		alloc.deallocate(memory, 16);
		REQUIRE(tracker.freeCount == 1);
		REQUIRE(tracker.liveBytes == 0);
	}

	SECTION("Over aligned types forward their alignment")
	{
		struct alignas(64) OverAligned
		{
			UInt8 data[64];
		};

		allocator<OverAligned> alloc(callbacks);
		OverAligned* memory = alloc.allocate(1);
		REQUIRE(tracker.lastAlignment == 64);
		alloc.deallocate(memory, 1);
	}

	SECTION("Allocation failure throws bad_alloc")
	{
		allocator<UInt32> alloc(callbacks);
		tracker.failNextAllocation = true;
		REQUIRE_THROWS_AS(alloc.allocate(1), std::bad_alloc);
		REQUIRE(tracker.allocationCount == 0);
	}

	SECTION("Element count overflow throws bad_alloc")
	{
		allocator<UInt64> alloc(callbacks);
		REQUIRE_THROWS_AS(alloc.allocate(alloc.max_size() + 1), std::bad_alloc);
		REQUIRE(tracker.allocationCount == 0);
	}
}

TEST_CASE("Vkd allocator - Scope factories", "[vkd][allocator]")
{
	AllocationTracker tracker;
	VkAllocationCallbacks callbacks = MakeCallbacks(tracker);

	SECTION("Each factory pre-fills its scope")
	{
		REQUIRE(CommandAllocator<UInt32>(callbacks).GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
		REQUIRE(ObjectAllocator<UInt32>(callbacks).GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		REQUIRE(CacheAllocator<UInt32>(callbacks).GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
		REQUIRE(DeviceAllocator<UInt32>(callbacks).GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		REQUIRE(InstanceAllocator<UInt32>(callbacks).GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	}

	SECTION("Factories return the plain allocator type, usable by the container aliases")
	{
		vkd::vector<UInt32> values(DeviceAllocator<UInt32>(callbacks));
		values.push_back(7);

		REQUIRE(tracker.lastScope == VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		REQUIRE(values.get_allocator() == ObjectAllocator<UInt32>(callbacks));
	}
}

TEST_CASE("Vkd allocator - Equality", "[vkd][allocator]")
{
	AllocationTracker first;
	AllocationTracker second;
	VkAllocationCallbacks firstCallbacks = MakeCallbacks(first);
	VkAllocationCallbacks sameAsFirst = MakeCallbacks(first);
	VkAllocationCallbacks secondCallbacks = MakeCallbacks(second);

	SECTION("Distinct structs sharing the same callbacks compare equal")
	{
		REQUIRE(allocator<UInt32>(firstCallbacks) == allocator<UInt32>(sameAsFirst));
	}

	SECTION("Different pUserData compares unequal")
	{
		REQUIRE(allocator<UInt32>(firstCallbacks) != allocator<UInt32>(secondCallbacks));
	}

	SECTION("The allocation scope does not take part in equality")
	{
		allocator<UInt32> objectScope(firstCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		allocator<UInt32> deviceScope(firstCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		REQUIRE(objectScope == deviceScope);
	}

	SECTION("Rebound allocators keep comparing equal")
	{
		allocator<UInt32> alloc(firstCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
		allocator<UInt64> rebound(alloc);
		REQUIRE(alloc == rebound);
		REQUIRE(rebound.GetAllocationScope() == VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	}
}

TEST_CASE("Vkd allocator - Container aliases", "[vkd][allocator][stl]")
{
	AllocationTracker tracker;
	VkAllocationCallbacks callbacks = MakeCallbacks(tracker);
	allocator<UInt32> alloc(callbacks, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

	SECTION("vector allocations go through the callbacks")
	{
		{
			vkd::vector<UInt32> values(alloc);
			for (UInt32 i = 0; i < 128; ++i)
				values.push_back(i);

			REQUIRE(tracker.allocationCount > 0);
			REQUIRE(tracker.liveBytes >= 128 * sizeof(UInt32));
			REQUIRE(values.get_allocator() == alloc);
			REQUIRE(tracker.lastScope == VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		}

		REQUIRE(tracker.liveBytes == 0);
		REQUIRE(tracker.allocationCount == tracker.freeCount);
	}

	SECTION("string allocations go through the callbacks")
	{
		{
			allocator<char> characterAlloc(alloc);
			vkd::string text(characterAlloc);
			text.assign(512, 'v'); // Long enough to defeat the small string optimization
			REQUIRE(tracker.allocationCount > 0);
		}

		REQUIRE(tracker.liveBytes == 0);
	}

	SECTION("unordered_map node allocations are rebound onto the callbacks")
	{
		{
			allocator<std::pair<const UInt32, UInt32>> nodeAlloc(alloc);
			vkd::unordered_map<UInt32, UInt32> map(nodeAlloc);
			for (UInt32 i = 0; i < 64; ++i)
				map.emplace(i, i * 2);

			REQUIRE(map.size() == 64);
			REQUIRE(tracker.allocationCount >= 64);
		}

		REQUIRE(tracker.liveBytes == 0);
		REQUIRE(tracker.allocationCount == tracker.freeCount);
	}

	SECTION("Move assignment propagates the allocator")
	{
		vkd::vector<UInt32> source(alloc);
		source.push_back(42);

		vkd::vector<UInt32> destination{allocator<UInt32>(mem::GetDefaultAllocationCallbacks())};
		destination = std::move(source);

		REQUIRE(destination.get_allocator() == alloc);
		REQUIRE(destination.front() == 42);
	}

	SECTION("Swap keeps each buffer with its own callbacks")
	{
		AllocationTracker otherTracker;
		VkAllocationCallbacks otherCallbacks = MakeCallbacks(otherTracker);

		vkd::vector<UInt32> first(alloc);
		first.push_back(1);
		allocator<UInt32> otherAlloc(otherCallbacks);
		vkd::vector<UInt32> second(otherAlloc);
		second.push_back(2);

		first.swap(second);

		REQUIRE(first.front() == 2);
		REQUIRE(second.front() == 1);
		REQUIRE(first.get_allocator() == otherAlloc);
		REQUIRE(second.get_allocator() == alloc);
	}
}

TEST_CASE("Vkd allocator - Driver default callbacks", "[vkd][allocator]")
{
	static_assert(!std::is_default_constructible_v<allocator<UInt32>>);

	vkd::vector<UInt32> values{allocator<UInt32>(mem::GetDefaultAllocationCallbacks())};
	for (UInt32 i = 0; i < 32; ++i)
		values.push_back(i);

	REQUIRE(values.size() == 32);
	REQUIRE(values.get_allocator() == allocator<UInt32>(mem::GetDefaultAllocationCallbacks()));
}

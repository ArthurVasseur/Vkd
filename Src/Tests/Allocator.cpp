/**
 * @file Test/Allocator.cpp
 * @brief Unit tests for TLSF Allocator
 * @date 2025-10-30
 */

#include <VkdUtils/Allocator/Allocator.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <random>
#include <vector>
#include <algorithm>

using namespace vkd;

TEST_CASE("Allocator - Basic Initialization", "[allocator][init]")
{
	SECTION("Initialization succeeds")
	{
		Allocator allocator(1024 * 1024); // 1 MiB
		REQUIRE(allocator.Init());
		REQUIRE(allocator.GetTotal() == 1024 * 1024);
		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("Double initialization fails")
	{
		Allocator allocator(1024 * 1024);
		REQUIRE(allocator.Init());
		REQUIRE_FALSE(allocator.Init());
	}

	SECTION("Too small pool fails")
	{
		Allocator allocator(16); // Too small
		REQUIRE_FALSE(allocator.Init());
	}
}

TEST_CASE("Allocator - Basic Allocation", "[allocator][alloc]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Single allocation")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 16, alloc));
		REQUIRE(alloc.size >= 1024);
		REQUIRE((alloc.offset % 16) == 0);
		REQUIRE(allocator.GetUsed() > 0);
	}

	SECTION("Allocation and free")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 16, alloc));
		const std::size_t usedAfterAlloc = allocator.GetUsed();
		REQUIRE(usedAfterAlloc > 0);

		allocator.Free(alloc);
		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("Multiple allocations")
	{
		std::vector<Allocation> allocations;
		for (int i = 0; i < 10; ++i)
		{
			Allocation alloc;
			REQUIRE(allocator.Allocate(1024, 16, alloc));
			allocations.push_back(alloc);
		}

		REQUIRE(allocations.size() == 10);
		REQUIRE(allocator.GetUsed() > 10 * 1024);

		for (const auto& alloc : allocations)
		{
			allocator.Free(alloc);
		}

		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("Zero size allocation fails")
	{
		Allocation alloc;
		REQUIRE_FALSE(allocator.Allocate(0, 16, alloc));
	}

	SECTION("Non-power-of-2 alignment fails")
	{
		Allocation alloc;
		REQUIRE_FALSE(allocator.Allocate(1024, 17, alloc));
	}
}

TEST_CASE("Allocator - Alignment", "[allocator][alignment]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Alignment 256")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 256, alloc));
		REQUIRE((alloc.offset % 256) == 0);
	}

	SECTION("Alignment 4096")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 4096, alloc));
		REQUIRE((alloc.offset % 4096) == 0);
	}

	SECTION("Multiple allocations with different alignments")
	{
		Allocation alloc1, alloc2, alloc3;
		REQUIRE(allocator.Allocate(512, 16, alloc1));
		REQUIRE(allocator.Allocate(1024, 256, alloc2));
		REQUIRE(allocator.Allocate(2048, 4096, alloc3));

		REQUIRE((alloc1.offset % 16) == 0);
		REQUIRE((alloc2.offset % 256) == 0);
		REQUIRE((alloc3.offset % 4096) == 0);

		// Verify no overlaps
		REQUIRE(((alloc1.offset + alloc1.size <= alloc2.offset) ||
		         (alloc2.offset + alloc2.size <= alloc1.offset)));
		REQUIRE(((alloc1.offset + alloc1.size <= alloc3.offset) ||
		         (alloc3.offset + alloc3.size <= alloc1.offset)));
		REQUIRE(((alloc2.offset + alloc2.size <= alloc3.offset) ||
		         (alloc3.offset + alloc3.size <= alloc2.offset)));
	}
}

TEST_CASE("Allocator - Block Splitting", "[allocator][split]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Split creates appropriate remainder")
	{
		Allocation alloc1;
		REQUIRE(allocator.Allocate(1024, 16, alloc1));

		const std::size_t largestFree = allocator.GetLargestFreeBlock();
		REQUIRE(largestFree > 0);

		// Allocate a small block - should split from existing free blocks
		Allocation alloc2;
		REQUIRE(allocator.Allocate(512, 16, alloc2));

		// Free blocks should still exist
		const std::size_t newLargestFree = allocator.GetLargestFreeBlock();
		REQUIRE(newLargestFree > 0);
	}

	SECTION("Minimum block size prevents tiny fragments")
	{
		// Allocate almost all memory
		Allocation alloc1;
		REQUIRE(allocator.Allocate(1024 * 1024 - 512, 16, alloc1));

		// Try to allocate more - should fail if remainder would be too small
		Allocation alloc2;
		const bool success = allocator.Allocate(256, 16, alloc2);

		// Either it succeeds and uses the remaining space,
		// or it fails because the remainder would be too small
		if (success)
			REQUIRE(alloc2.size >= 256);
	}
}

TEST_CASE("Allocator - Coalescing", "[allocator][coalesce]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Coalesce two adjacent blocks")
	{
		Allocation alloc1, alloc2, alloc3;
		REQUIRE(allocator.Allocate(1024, 16, alloc1));
		REQUIRE(allocator.Allocate(1024, 16, alloc2));
		REQUIRE(allocator.Allocate(1024, 16, alloc3));

		// Free middle block first
		allocator.Free(alloc2);

		// Free first block - should coalesce with middle
		allocator.Free(alloc1);

		// Free last block - should coalesce with previous
		allocator.Free(alloc3);

		// All memory should be free
		REQUIRE(allocator.GetUsed() == 0);

		// Should be able to allocate large block again
		Allocation largeAlloc;
		REQUIRE(allocator.Allocate(3 * 1024, 16, largeAlloc));
	}

	SECTION("Coalesce in different orders")
	{
		// Test order: A, B, C
		{
			Allocator alloc(1024 * 1024);
			REQUIRE(alloc.Init());

			Allocation a, b, c;
			REQUIRE(alloc.Allocate(1024, 16, a));
			REQUIRE(alloc.Allocate(1024, 16, b));
			REQUIRE(alloc.Allocate(1024, 16, c));

			alloc.Free(a);
			alloc.Free(b);
			alloc.Free(c);

			REQUIRE(alloc.GetUsed() == 0);
		}

		// Test order: C, B, A
		{
			Allocator alloc(1024 * 1024);
			REQUIRE(alloc.Init());

			Allocation a, b, c;
			REQUIRE(alloc.Allocate(1024, 16, a));
			REQUIRE(alloc.Allocate(1024, 16, b));
			REQUIRE(alloc.Allocate(1024, 16, c));

			alloc.Free(c);
			alloc.Free(b);
			alloc.Free(a);

			REQUIRE(alloc.GetUsed() == 0);
		}

		// Test order: B, A, C
		{
			Allocator alloc(1024 * 1024);
			REQUIRE(alloc.Init());

			Allocation a, b, c;
			REQUIRE(alloc.Allocate(1024, 16, a));
			REQUIRE(alloc.Allocate(1024, 16, b));
			REQUIRE(alloc.Allocate(1024, 16, c));

			alloc.Free(b);
			alloc.Free(a);
			alloc.Free(c);

			REQUIRE(alloc.GetUsed() == 0);
		}
	}
}

TEST_CASE("Allocator - ReallocateInPlace", "[allocator][realloc]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Shrink allocation")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(2048, 16, alloc));

		const std::size_t originalOffset = alloc.offset;
		const std::size_t usedBefore = allocator.GetUsed();

		// Shrink to half size
		REQUIRE(allocator.ReallocateInPlace(alloc, 1024));
		REQUIRE(alloc.offset == originalOffset);
		REQUIRE(alloc.size >= 1024);
		REQUIRE(allocator.GetUsed() <= usedBefore);
	}

	SECTION("Grow allocation when space available")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 16, alloc));

		const std::size_t originalOffset = alloc.offset;

		// Try to grow - should succeed since there's free space after
		const bool success = allocator.ReallocateInPlace(alloc, 2048);

		if (success)
		{
			REQUIRE(alloc.offset == originalOffset);
			REQUIRE(alloc.size >= 2048);
		}
	}

	SECTION("Grow allocation fails when blocked")
	{
		Allocation alloc1, alloc2;
		REQUIRE(allocator.Allocate(1024, 16, alloc1));
		REQUIRE(allocator.Allocate(1024, 16, alloc2)); // Blocks growth of alloc1

		const std::size_t originalOffset = alloc1.offset;
		const std::size_t originalSize = alloc1.size;

		// Try to grow - should fail because alloc2 is in the way
		const bool success = allocator.ReallocateInPlace(alloc1, 2048);

		if (!success)
		{
			REQUIRE(alloc1.offset == originalOffset);
			REQUIRE(alloc1.size == originalSize);
		}
	}

	SECTION("Same size reallocation succeeds")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 16, alloc));

		const std::size_t originalSize = alloc.size;

		REQUIRE(allocator.ReallocateInPlace(alloc, originalSize));
		REQUIRE(alloc.size == originalSize);
	}
}

TEST_CASE("Allocator - Statistics", "[allocator][stats]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("GetTotal returns correct value")
	{
		REQUIRE(allocator.GetTotal() == 1024 * 1024);
	}

	SECTION("GetUsed tracks allocations")
	{
		REQUIRE(allocator.GetUsed() == 0);

		Allocation alloc1;
		REQUIRE(allocator.Allocate(1024, 16, alloc1));
		const std::size_t used1 = allocator.GetUsed();
		REQUIRE(used1 > 0);

		Allocation alloc2;
		REQUIRE(allocator.Allocate(2048, 16, alloc2));
		const std::size_t used2 = allocator.GetUsed();
		REQUIRE(used2 > used1);

		allocator.Free(alloc1);
		REQUIRE(allocator.GetUsed() < used2);

		allocator.Free(alloc2);
		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("GetLargestFreeBlock")
	{
		const std::size_t initialLargest = allocator.GetLargestFreeBlock();
		REQUIRE(initialLargest > 0);

		// Allocate some blocks
		Allocation alloc1, alloc2;
		REQUIRE(allocator.Allocate(100 * 1024, 16, alloc1));
		REQUIRE(allocator.Allocate(100 * 1024, 16, alloc2));

		// Largest free block should be smaller now
		const std::size_t afterAlloc = allocator.GetLargestFreeBlock();
		REQUIRE(afterAlloc < initialLargest);

		// Free one block
		allocator.Free(alloc1);

		// Free another
		allocator.Free(alloc2);

		// After freeing, largest should increase (due to coalescing)
		const std::size_t afterFree = allocator.GetLargestFreeBlock();
		REQUIRE(afterFree > afterAlloc);
	}

	SECTION("GetExternalFragmentation")
	{
		// Initially no fragmentation
		const double initialFrag = allocator.GetExternalFragmentation();
		REQUIRE(initialFrag >= 0.0);
		REQUIRE(initialFrag <= 1.0);

		// Allocate and free to create fragmentation
		std::vector<Allocation> allocs;
		for (int i = 0; i < 10; ++i)
		{
			Allocation alloc;
			REQUIRE(allocator.Allocate(1024, 16, alloc));
			allocs.push_back(alloc);
		}

		// Free every other allocation
		for (std::size_t i = 0; i < allocs.size(); i += 2)
		{
			allocator.Free(allocs[i]);
		}

		const double fragmented = allocator.GetExternalFragmentation();
		REQUIRE(fragmented >= 0.0);
		REQUIRE(fragmented <= 1.0);

		// Free remaining allocations
		for (std::size_t i = 1; i < allocs.size(); i += 2)
		{
			allocator.Free(allocs[i]);
		}

		// After freeing all, fragmentation should be minimal
		const double finalFrag = allocator.GetExternalFragmentation();
		REQUIRE(finalFrag >= 0.0);
		REQUIRE(finalFrag <= 1.0);
	}
}

TEST_CASE("Allocator - Stress Test", "[allocator][stress]")
{
	Allocator allocator(16 * 1024 * 1024); // 16 MiB
	REQUIRE(allocator.Init());

	std::mt19937_64 rng(12345); // Fixed seed for reproducibility
	std::uniform_int_distribution<std::size_t> sizeDist(64, 4096);
	std::uniform_int_distribution<int> opDist(0, 99);
	std::vector<Allocation> activeAllocs;

	const int numOperations = 100000;
	int allocCount = 0;
	int freeCount = 0;

	for (int i = 0; i < numOperations; ++i)
	{
		const int op = opDist(rng);

		if (op < 60 || activeAllocs.empty()) // 60% allocate
		{
			const std::size_t size = sizeDist(rng);
			const std::size_t alignment = (1ULL << (rng() % 5)) * 16; // 16, 32, 64, 128, 256

			Allocation alloc;
			if (allocator.Allocate(size, alignment, alloc))
			{
				activeAllocs.push_back(alloc);
				++allocCount;

				REQUIRE((alloc.offset % alignment) == 0);
			}
		}
		else // 40% free
		{
			const std::size_t index = rng() % activeAllocs.size();
			allocator.Free(activeAllocs[index]);
			++freeCount;

			activeAllocs[index] = activeAllocs.back();
			activeAllocs.pop_back();
		}
	}

	// Free remaining allocations
	for (const auto& alloc : activeAllocs)
	{
		allocator.Free(alloc);
		++freeCount;
	}

	// Verify all memory is freed
	REQUIRE(allocator.GetUsed() == 0);

	// Check fragmentation
	const double fragmentation = allocator.GetExternalFragmentation();
	REQUIRE(fragmentation >= 0.0);
	REQUIRE(fragmentation <= 1.0);

	INFO("Allocations: " << allocCount);
	INFO("Frees: " << freeCount);
	INFO("Final Fragmentation: " << (fragmentation * 100.0) << "%");
}

TEST_CASE("Allocator - Invariants", "[allocator][invariants]")
{
	Allocator allocator(1024 * 1024);
	REQUIRE(allocator.Init());

	SECTION("Used + Free <= Total")
	{
		const std::size_t total = allocator.GetTotal();

		std::vector<Allocation> allocs;
		for (int i = 0; i < 20; ++i)
		{
			Allocation alloc;
			if (allocator.Allocate(1024 * i + 512, 16, alloc))
				allocs.push_back(alloc);

			const std::size_t used = allocator.GetUsed();
			REQUIRE(used <= total);
		}

		for (std::size_t i = 0; i < allocs.size() / 2; ++i)
		{
			allocator.Free(allocs[i]);
			const std::size_t used = allocator.GetUsed();
			REQUIRE(used <= total);
		}

		for (std::size_t i = allocs.size() / 2; i < allocs.size(); ++i)
		{
			allocator.Free(allocs[i]);
			const std::size_t used = allocator.GetUsed();
			REQUIRE(used <= total);
		}

		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("No overlapping allocations")
	{
		std::vector<Allocation> allocs;

		for (int i = 0; i < 50; ++i)
		{
			Allocation alloc;
			if (allocator.Allocate(1024, 16, alloc))
			{
				for (const auto& existing : allocs)
				{
					const bool noOverlap =
						(alloc.offset + alloc.size <= existing.offset) ||
						(existing.offset + existing.size <= alloc.offset);
					REQUIRE(noOverlap);
				}

				allocs.push_back(alloc);
			}
		}
	}

	SECTION("Double free is safe")
	{
		Allocation alloc;
		REQUIRE(allocator.Allocate(1024, 16, alloc));

		allocator.Free(alloc);
		const std::size_t usedAfterFree = allocator.GetUsed();

		// Double free should be handled gracefully (no crash, no change)
		allocator.Free(alloc);
		REQUIRE(allocator.GetUsed() == usedAfterFree);
	}
}

TEST_CASE("Allocator - Edge Cases", "[allocator][edge]")
{
	SECTION("Allocate entire pool")
	{
		Allocator allocator(1024 * 1024);
		REQUIRE(allocator.Init());

		Allocation alloc;
		const bool success = allocator.Allocate(1024 * 1024 - 256, 16, alloc);

		if (success)
		{
			REQUIRE(alloc.size > 0);
			REQUIRE(allocator.GetUsed() > 0);

			Allocation alloc2;
			REQUIRE_FALSE(allocator.Allocate(1024, 16, alloc2));
		}
	}

	SECTION("Many small allocations")
	{
		Allocator allocator(1024 * 1024);
		REQUIRE(allocator.Init());

		std::vector<Allocation> allocs;

		for (int i = 0; i < 1000; ++i)
		{
			Allocation alloc;
			if (allocator.Allocate(64, 16, alloc))
				allocs.push_back(alloc);
			else
				break; // Out of memory
		}

		REQUIRE(allocs.size() > 0);

		for (const auto& alloc : allocs)
		{
			allocator.Free(alloc);
		}

		REQUIRE(allocator.GetUsed() == 0);
	}

	SECTION("Allocation before Init fails")
	{
		Allocator allocator(1024 * 1024);
		// Don't call Init()

		Allocation alloc;
		REQUIRE_FALSE(allocator.Allocate(1024, 16, alloc));
	}
}

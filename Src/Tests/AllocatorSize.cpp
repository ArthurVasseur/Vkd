/**
 * @file AllocatorSize.cpp
 * @brief Allocator size computation test
 * @date 2025-10-30
 *
 * Tests device memory heap size calculation based on system RAM.
 */

#include <iostream>

#include <VkdUtils/Allocator/Allocator.hpp>
#include <VkdUtils/System/System.hpp>

// int main()
// {
// 	vkd::utils::System system;
// 	const cct::UInt64 totalRam = system.GetTotalRamBytes();
// 	const cct::UInt64 heapSize = vkd::utils::System::ComputeDeviceMemoryHeapSize(totalRam);

// 	std::cout << "Total RAM: " << (totalRam / (1024 * 1024)) << " MB" << std::endl;
// 	std::cout << "30% of RAM: " << ((totalRam * 3) / (10 * 1024 * 1024)) << " MB" << std::endl;
// 	std::cout << "Allocator size (rounded to power of 2): " << (heapSize / (1024 * 1024)) << " MB" << std::endl;

// 	vkd::Allocator allocator(heapSize);
// 	std::cout << "Allocator initialized with " << (allocator.GetTotal() / (1024 * 1024)) << " MB" << std::endl;

// 	return 0;
// }

/**
 * @file DescriptorSet.cpp
 * @brief Implementation of DescriptorSet
 * @date 2026-08-25
 */

#include "Vkd/DescriptorSet/DescriptorSet.hpp"

namespace vkd
{
	void DescriptorSet::SetBufferBinding(UInt32 binding, UInt32 arrayElement, const BufferBinding& info)
	{
		AssertValid();

		std::vector<BufferBinding>& bindings = m_bufferBindings[binding];
		if (arrayElement >= bindings.size())
			bindings.resize(arrayElement + 1);

		bindings[arrayElement] = info;
	}

	const DescriptorSet::BufferBinding* DescriptorSet::FindBufferBinding(UInt32 binding, UInt32 arrayElement) const noexcept
	{
		AssertValid();

		auto it = m_bufferBindings.find(binding);
		if (it == m_bufferBindings.end() || arrayElement >= it->second.size())
			return nullptr;

		return &it->second[arrayElement];
	}
} // namespace vkd

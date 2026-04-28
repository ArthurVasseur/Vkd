/**
 * @file DescriptorSetLayout.cpp
 * @brief Implementation of DescriptorSetLayout
 * @date 2026-04-28
 */

#include "Vkd/DescriptorSetLayout/DescriptorSetLayout.hpp"

#include <algorithm>

namespace vkd
{
	const DescriptorSetLayout::Binding* DescriptorSetLayout::FindBinding(UInt32 bindingIndex) const noexcept
	{
		AssertValid();
		auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [bindingIndex](const Binding& b)
							   { return b.binding == bindingIndex; });
		if (it == m_bindings.end())
			return nullptr;
		return &*it;
	}
} // namespace vkd

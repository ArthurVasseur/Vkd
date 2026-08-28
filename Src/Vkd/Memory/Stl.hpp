/**
 * @file Stl.hpp
 * @brief Standard container aliases bound to the Vkd allocator
 * @date 2026-08-27
 */

#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Vkd/Memory/Allocator.hpp"

namespace vkd
{
	template<typename T>
	using vector = std::vector<T, allocator<T>>;

	template<typename CharT, typename Traits = std::char_traits<CharT>>
	using basic_string = std::basic_string<CharT, Traits, allocator<CharT>>;

	using string = basic_string<char>;

	template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
	using unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, allocator<std::pair<const Key, T>>>;

	template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
	using unordered_set = std::unordered_set<Key, Hash, KeyEqual, allocator<Key>>;
} // namespace vkd

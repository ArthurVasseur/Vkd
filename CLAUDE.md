# Coding Style Guide - Vkd Project

This document describes the coding conventions used in the Vkd (Vulkan Driver) project.

## Table of Contents

- [Indentation and Formatting](#indentation-and-formatting)
- [Naming](#naming)
- [Includes](#includes)
- [Comments](#comments)
- [Control Structures](#control-structures)
- [Constructors](#constructors)
- [Types](#types)
- [Flags and Enums](#flags-and-enums)

---

## Indentation and Formatting

### Indentation
- **Use tabs only** (no spaces for indentation)
- One tab per indentation level

### Braces (Allman style)
- Opening braces must be on their own line
- Exception: lambdas and inline initializations may use braces on the same line

```cpp
// ✅ Correct
void Function()
{
	if (condition)
	{
		DoSomething();
	}
}

// ❌ Incorrect
void Function() {
	if (condition) {
		DoSomething();
	}
}
```

### Simple if/else Blocks
- If the body of an `if`, `else`, `for`, or `while` contains a single statement, **do not use braces**

```cpp
// ✅ Correct
if (condition)
	DoSomething();
else
	DoSomethingElse();

// ❌ Incorrect
if (condition)
{
	DoSomething();
}
else
{
	DoSomethingElse();
}
```

---

## Naming

### Member Variables
- Prefix all member variables with `m_`
- Use camelCase after the prefix

```cpp
class Example
{
private:
	int m_Count;
	std::string m_Name;
	bool m_IsInitialized;
};
```

### Constants
- Use **PascalCase** (first letter capitalized)
- **Do not use** the `k` prefix (old convention)

```cpp
// ✅ Correct
static constexpr std::size_t MinBlockSize = 32;
static constexpr UInt32 DefaultFirstLevelIndexBits = 5;

// ❌ Incorrect
static constexpr std::size_t kMinBlockSize = 32;
static constexpr UInt32 kDefaultFirstLevelIndexBits = 5;
```

### Functions and Methods
- Use **PascalCase** for public functions
- No acronyms: use complete and explicit names

```cpp
// ✅ Correct
UInt32 FindLastSet64(UInt64 x);
void Mapping(std::size_t size, UInt32& outFirstLevelIndex, UInt32& outSecondLevelIndex);

// ❌ Incorrect (acronyms)
UInt32 Fls64(UInt64 x);
void Mapping(std::size_t size, UInt32& outFLI, UInt32& outSLI);
```

### Types and Classes
- Use **PascalCase**

```cpp
class Allocator { };
struct Block { };
enum class BlockFlag : UInt32 { };
```

---

## Includes

### Include Order
1. Corresponding header (if .cpp file)
2. Project headers (with full paths)
3. External library headers (Concerto, etc.)
4. System headers (STL, etc.)

### Include Format
- Use **full paths** for project headers
- Format: `<Module/Submodule/File.hpp>` or `"Module/Submodule/File.hpp"`

```cpp
// ✅ Correct
#include "VkdUtils/Allocator/Allocator.hpp"
#include <Concerto/Core/Types/Types.hpp>
#include <algorithm>

// ❌ Incorrect (relative path)
#include "Allocator.hpp"
#include "../Memory/Memory.hpp"
```

---

## Comments

### General Principle
- **Avoid obvious comments**: the function/variable name should be sufficiently self-explanatory
- Comments should explain the **why**, not the **what**
- Use Doxygen only when it adds non-trivial information

### Public Functions
- Document with Doxygen **only if** the behavior is not obvious from the name and signature
- If the name is explicit (e.g., `GetTotal()`, `GetUsed()`), do not comment

```cpp
// ✅ Correct - explicit name, no comment needed
std::size_t GetTotal() const noexcept;
std::size_t GetUsed() const noexcept;

// ✅ Correct - complex behavior, useful comment
/**
 * @brief Initialize the allocator and set up internal structures
 * @return true if initialization succeeded, false otherwise
 * @note Must be called before any allocation operations
 * @note No dynamic allocations will occur after this call
 */
bool Init() noexcept;

// ❌ Incorrect - redundant comment
/// Get the total size
std::size_t GetTotal() const noexcept;
```

### Member Variables
- Do not comment if the name is explicit

```cpp
// ✅ Correct
private:
	std::size_t m_TotalSize;
	std::size_t m_UsedSize;
	bool m_IsInitialized;

// ❌ Incorrect - redundant comments
private:
	/// Total size of the memory pool
	std::size_t m_TotalSize;
	/// Currently used memory
	std::size_t m_UsedSize;
	/// Flag indicating if initialized
	bool m_IsInitialized;
```

### Inline Comments
- Remove comments like "Update X", "Calculate Y" that merely repeat the code
- Keep only comments that explain special cases or implementation choices

```cpp
// ✅ Correct
if (size < MinBlockSize)
	size = MinBlockSize;

// ❌ Incorrect
// Ensure minimum size
if (size < MinBlockSize)
	size = MinBlockSize;
```

### Section Separator Comments
- **Do not use** decorative separator comments with lines of `=` or `-`
- Let the code structure and organization speak for itself

```cpp
// ❌ Incorrect - decorative separators
// ============================================================
// Test: Block Splitting
// ============================================================
TEST_CASE("Allocator - Block Splitting", "[allocator][split]")
{
	// ...
}

// ✅ Correct - no separators
TEST_CASE("Allocator - Block Splitting", "[allocator][split]")
{
	// ...
}
```

---

## Control Structures

### Return Early
- Use early `return` statements for error cases
- Avoid unnecessary `else` after a `return`

```cpp
// ✅ Correct
bool Function()
{
	if (!m_Initialized)
		return false;

	if (size == 0)
		return false;

	DoWork();
	return true;
}

// ❌ Incorrect
bool Function()
{
	if (!m_Initialized)
	{
		return false;
	}
	else
	{
		if (size == 0)
		{
			return false;
		}
		else
		{
			DoWork();
			return true;
		}
	}
}
```

---

## Constructors

### Initialization List
- Put the `:` on the same line as the constructor signature
- Put **commas at the end** of each line (not at the beginning)
- No comma on the last line

```cpp
// ✅ Correct
Allocator::Allocator(std::size_t poolSizeBytes) noexcept :
	m_TotalSize(poolSizeBytes),
	m_UsedSize(0),
	m_Pool(nullptr),
	m_FirstLevelIndexBits(DefaultFirstLevelIndexBits),
	m_Initialized(false)
{
}

// ❌ Incorrect (commas at the beginning)
Allocator::Allocator(std::size_t poolSizeBytes) noexcept
	: m_TotalSize(poolSizeBytes)
	, m_UsedSize(0)
	, m_Pool(nullptr)
	, m_Initialized(false)
{
}
```

---

## Types

### Use Concerto Types
- Use Concerto framework types instead of standard types
- Namespace: `cct::`

```cpp
// ✅ Correct
UInt8, UInt32, UInt64
Int32, Int64

// ❌ Incorrect
uint8_t, uint32_t, uint64_t
int32_t, int64_t
std::uint8_t, std::uint32_t
```

### Pointers and References
- Attach the asterisk or ampersand to the **type**, not the variable name

```cpp
// ✅ Correct
Block* block;
const Block* ptr;
Allocation& alloc;

// ❌ Incorrect
Block *block;
Block * block;
Allocation &alloc;
```

---

## Flags and Enums

### EnumFlags
- Use Concerto's `EnumFlags<>` for type-safe flags
- Define enum class with underlying type
- Enable `EnumFlags` with `CCT_ENABLE_ENUM_FLAGS()` in global namespace

```cpp
// In the .cpp file
enum class BlockFlag : UInt32
{
	None = 0,
	IsFree = 0x1,
	PrevIsFree = 0x2
};

// In global namespace (after vkd namespace)
CCT_ENABLE_ENUM_FLAGS(vkd::BlockFlag)

// Usage
struct Block
{
	cct::EnumFlags<BlockFlag> flags;

	bool IsFree() const noexcept
	{
		return flags.Contains(BlockFlag::IsFree);
	}

	void SetFree(bool free) noexcept
	{
		if (free)
			flags.Set(BlockFlag::IsFree);
		else
			flags.Reset(BlockFlag::IsFree);
	}
};
```

---

## Platform Macros

### Use Concerto Platform Macros
- **Do not use** compiler macros directly (`_MSC_VER`, `__GNUC__`, etc.)
- Use platform macros: `CCT_PLATFORM_WINDOWS`, `CCT_PLATFORM_POSIX`, etc.

```cpp
// ✅ Correct
#if defined(CCT_PLATFORM_WINDOWS)
	#include <intrin.h>
	unsigned long index;
	_BitScanReverse64(&index, x);
#elif defined(CCT_PLATFORM_POSIX)
	return 63 - __builtin_clzll(x);
#else
	// Portable fallback
#endif

// ❌ Incorrect
#if defined(_MSC_VER)
	#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
	return 63 - __builtin_clzll(x);
#endif
```

---

## Memory Management

### Smart Pointers
- Use `std::unique_ptr` instead of `new`/`delete`
- Do not use raw pointers for resource management

```cpp
// ✅ Correct
m_Pool = std::unique_ptr<UInt8[]>(new (std::nothrow) UInt8[size]);

// ❌ Incorrect
m_Pool = new UInt8[size];
// ...
delete[] m_Pool;
```

---

## Complex Structure Documentation

### ASCII Diagrams
- For complex data structures, add ASCII diagrams to the documentation
- Use Unicode box drawings for better clarity

```cpp
/**
 * PHYSICAL LAYOUT IN MEMORY:
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │  Pool (contiguous memory)                                             │
 * ├─────────────┬─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Block 1   │   Block 2   │   Block 3   │   Block 4   │   Block 5     │
 * │  (Alloc)    │   (Free)    │  (Alloc)    │   (Free)    │   (Free)      │
 * └─────────────┴─────────────┴─────────────┴─────────────┴───────────────┘
 */
```
Never add:
```
🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
```

To the commit message.

---

## Key Points Summary

1. ✅ Tabs for indentation (never spaces)
2. ✅ Allman braces (on their own line)
3. ✅ No braces for single-statement `if`/`else`/`for`/`while`
4. ✅ Member variables prefixed with `m_`
5. ✅ Constants in PascalCase (no `k` prefix)
6. ✅ No acronyms in function names
7. ✅ Includes with full paths
8. ✅ Commas at the end in initialization lists
9. ✅ Comments only when non-obvious
10. ✅ Concerto types (`UInt32`, `UInt64`, etc.)
11. ✅ EnumFlags for type-safe flags
12. ✅ Concerto platform macros (`CCT_PLATFORM_*`)
13. ✅ `std::unique_ptr` instead of `new`/`delete`

---

*Maintained document - Version 1.0*

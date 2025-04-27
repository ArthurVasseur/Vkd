Vkd is a lightweight, educational implementation of a Vulkan driver written in modern C++ and built with [xmake]. It’s designed from the ground up to explore the inner workings of a Vulkan-compatible driver, including instance and device creation, memory management, and the Installable Client Driver (ICD) interface.

# Getting Started

## Prerequisites

- A C++20-capable compiler (e.g. GCC 10+, Clang 11+, MSVC 2019+)
- xmake


## Clone & Build

```
git clone https://github.com/ArthurVasseur/Vkd.git
cd Vkd
xmake      # builds the shared library “vkd”
```

## Project Structure

```
Vkd/
├─ Src/
│  ├─ Vkd/
│  │  ├─ Icd/               # ICD entry points & loader interface
│  │  ├─ Instance/          # vkCreateInstance, vkEnumeratePhysicalDevices, etc.
│  │  ├─ PhysicalDevice/    # Properties, features enumeration
│  │  ├─ Device/            # vkCreateDevice, queues, command buffers
│  │  ├─ Memory/            # Allocation, mapping, binding logic
│  │  ├─ ObjectBase/        # Handle dispatch and object lifetime
│  │  └─ Defines.hpp        # Common Vulkan defines and macros
│  └─ xmake.lua             # Build system
├─ LICENSE                  # MIT License
└─ README.md                # (You are here!)
```
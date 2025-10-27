# Vkd – A Software Vulkan Driver

![Status](https://img.shields.io/badge/status-experimental-orange)
![Language](https://img.shields.io/badge/language-C++20-blue)
![License](https://img.shields.io/badge/license-MIT-green)

**Vkd** is an experimental Vulkan **Installable Client Driver (ICD)** written in modern **C++20**, built to explore the design of a software-only Vulkan implementation.  
It serves as a learning and prototyping platform for Vulkan driver development.

---

## ✨ Features

- Implements the Vulkan ICD interface (`vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`, etc.)
- CPU-based **software physical device**
- Basic Vulkan instance & device creation
- Custom memory allocation via [mimalloc](https://github.com/microsoft/mimalloc)
- Cross-platform support (Windows / Linux)
- Extensible design for future hardware backends

> 🧪 Note: This project is under active development. Many Vulkan features are currently stubbed or not yet implemented.

---

## 🧱 Project Structure

```
Src/
 ├── Vkd/                  # Core ICD and Vulkan object implementations
 │   ├── Device/           # VkDevice abstraction
 │   ├── Instance/         # VkInstance and creation routines
 │   ├── PhysicalDevice/   # Base physical device representation
 │   ├── Icd/              # Vulkan loader / ICD entry points
 │   ├── Synchronization/  # Fences, semaphores, etc.
 │   └── ...
 └── VkdSoftware/          # Software (CPU) driver implementation
     ├── Device/
     ├── PhysicalDevice/
     ├── Queue/
     ├── Buffer/
     └── ...
```

---

## ⚙️ Building

Vkd uses [xmake](https://xmake.io) as its build system.

### Prerequisites

- C++20 compiler (MSVC, Clang, or GCC)
- [xmake](https://xmake.io/#/getting_started)
- Internet connection (for package dependencies)

### Build steps

```bash
# Clone the repository
git clone https://github.com/ArthurVasseur/Vkd.git
cd Vkd

# Configure (debug mode by default)
xmake f -m debug --debug_checks=y

# Build the project
xmake

# Run tests
xmake run vkd-test
```

---


## 🚧 Current Status

| Component | Status | Description |
|------------|---------|-------------|
| ICD negotiation | ✅ Implemented | Handles Vulkan loader communication |
| Instance / Physical Device | ✅ Partial | Software device creation on CPU |
| Queues / Command Pools | ⚙️ WIP | Stubbed but structurally complete |
| Memory management | ⚙️ WIP | Custom allocator integrated |
| Rendering pipeline | 🚫 Not implemented | Planned for future |
| Validation / Layers | 🚫 Not implemented | To be added later |

---

## 🧠 Design Goals

- Provide a **reference software Vulkan implementation** for learning and experimentation
- Explore **driver-level debugging**, **object lifetime tracking**
- Serve as a foundation for **future hardware driver backends**

---

## 🤝 Contributing

Contributions, bug reports, and discussions are welcome!  
This project is still in early development — feel free to open an issue or pull request if you want to help.

---

## 📄 License

This project is licensed under the [MIT License](./LICENSE).  
Copyright (c) 2025 **Arthur Vasseur**
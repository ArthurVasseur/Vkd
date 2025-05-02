//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "WddmDump/Api/Instance.hpp"
#include "Concerto/Graphics/RHI/Instance.hpp"
#include "Concerto/Graphics/RHI/APIImpl.hpp"

namespace wddmDump::vk
{
	class Instance : public wddmDump::Instance
	{
	public:
		Instance() = default;
		~Instance() override = default;

		std::size_t GetDeviceCount() override;
		std::unique_ptr<wddmDump::Device> CreateDevice(std::size_t index) override;
	private:
		cct::gfx::rhi::Instance m_instance;
	};
}
namespace vkd
{
	inline ShaderModule::ShaderModule(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_code(ObjectAllocator<cct::UInt32>(allocationCallbacks))
	{
	}

	inline Device* ShaderModule::GetOwner() const
	{
		return m_owner;
	}

	inline const vkd::vector<cct::UInt32>& ShaderModule::GetCode() const
	{
		return m_code;
	}

	inline cct::UInt32 ShaderModule::GetCodeSize() const
	{
		return static_cast<cct::UInt32>(m_code.size() * sizeof(cct::UInt32));
	}
} // namespace vkd

namespace vkd
{
	inline ShaderModule::ShaderModule() :
		ObjectBase(ObjectType),
		m_owner(nullptr)
	{
	}

	inline Device* ShaderModule::GetOwner() const
	{
		return m_owner;
	}

	inline const std::vector<cct::UInt32>& ShaderModule::GetCode() const
	{
		return m_code;
	}

	inline cct::UInt32 ShaderModule::GetCodeSize() const
	{
		return static_cast<cct::UInt32>(m_code.size() * sizeof(cct::UInt32));
	}
}

/**
 * @file Module.cpp
 * @brief Implementation of the SPIR-V frontend parser
 * @date 2026-08-24
 */

#include "See/Spirv/Module.hpp"

#include <Concerto/Core/Logger/Logger.hpp>

#include <spirv-tools/libspirv.h>
#include <spirv/unified1/spirv.h>

namespace see::spirv
{
	namespace
	{
		struct ParseState
		{
			Module m_module;
			Function* m_currentFunction = nullptr;
			BasicBlock* m_currentBlock = nullptr;
		};

		Terminator ToTerminator(SpvOp opcode)
		{
			switch (opcode)
			{
				case SpvOpBranch:
					return Terminator::Branch;
				case SpvOpBranchConditional:
					return Terminator::BranchConditional;
				case SpvOpSwitch:
					return Terminator::Switch;
				case SpvOpReturn:
					return Terminator::Return;
				case SpvOpReturnValue:
					return Terminator::ReturnValue;
				case SpvOpKill:
					return Terminator::Kill;
				case SpvOpUnreachable:
					return Terminator::Unreachable;
				default:
					return Terminator::None;
			}
		}

		void FillTerminatorData(BasicBlock& block, SpvOp opcode, const spv_parsed_instruction_t& instruction)
		{
			switch (opcode)
			{
				case SpvOpBranch:
					block.m_branchTargets.push_back(instruction.words[instruction.operands[0].offset]);
					break;
				case SpvOpBranchConditional:
					block.m_terminatorOperand = instruction.words[instruction.operands[0].offset];
					block.m_branchTargets.push_back(instruction.words[instruction.operands[1].offset]);
					block.m_branchTargets.push_back(instruction.words[instruction.operands[2].offset]);
					break;
				case SpvOpSwitch:
					// Selector, Default, (Literal, Label)*
					block.m_terminatorOperand = instruction.words[instruction.operands[0].offset];
					block.m_branchTargets.push_back(instruction.words[instruction.operands[1].offset]);
					for (cct::UInt16 i = 3; i < instruction.num_operands; i += 2)
						block.m_branchTargets.push_back(instruction.words[instruction.operands[i].offset]);
					break;
				case SpvOpReturnValue:
					block.m_terminatorOperand = instruction.words[instruction.operands[0].offset];
					break;
				default:
					break;
			}
		}

		void HandleEntryPoint(Module& module, const spv_parsed_instruction_t& instruction)
		{
			EntryPoint entryPoint;
			entryPoint.m_executionModel = instruction.words[instruction.operands[0].offset];

			bool foundFunctionId = false;
			for (cct::UInt16 i = 0; i < instruction.num_operands; ++i)
			{
				const spv_parsed_operand_t& operand = instruction.operands[i];
				if (operand.type == SPV_OPERAND_TYPE_LITERAL_STRING)
					entryPoint.m_name = reinterpret_cast<const char*>(&instruction.words[operand.offset]);
				else if (operand.type == SPV_OPERAND_TYPE_ID)
				{
					if (!foundFunctionId)
					{
						entryPoint.m_functionId = instruction.words[operand.offset];
						foundFunctionId = true;
					}
					else
						entryPoint.m_interfaceIds.push_back(instruction.words[operand.offset]);
				}
			}

			module.m_entryPoints.push_back(std::move(entryPoint));
		}

		void HandleDecorate(Module& module, const spv_parsed_instruction_t& instruction)
		{
			if (instruction.num_operands < 2)
				return;

			const cct::UInt32 target = instruction.words[instruction.operands[0].offset];

			Decoration decoration;
			decoration.m_kind = instruction.words[instruction.operands[1].offset];
			for (cct::UInt16 i = 2; i < instruction.num_operands; ++i)
				decoration.m_literals.push_back(instruction.words[instruction.operands[i].offset]);

			module.m_decorations[target].push_back(std::move(decoration));
		}

		Instruction ToInstruction(const spv_parsed_instruction_t& instruction)
		{
			Instruction result;
			result.m_opcode = instruction.opcode;
			result.m_resultId = instruction.result_id;
			result.m_typeId = instruction.type_id;
			result.m_operands.assign(instruction.words + 1, instruction.words + instruction.num_words);
			return result;
		}

		spv_result_t OnInstruction(void* userData, const spv_parsed_instruction_t* instruction)
		{
			auto& state = *static_cast<ParseState*>(userData);
			const SpvOp opcode = static_cast<SpvOp>(instruction->opcode);

			if (opcode == SpvOpEntryPoint)
			{
				HandleEntryPoint(state.m_module, *instruction);
				return SPV_SUCCESS;
			}

			if (opcode == SpvOpDecorate)
			{
				HandleDecorate(state.m_module, *instruction);
				return SPV_SUCCESS;
			}

			if (opcode == SpvOpFunction)
			{
				state.m_module.m_functions.push_back(Function{.m_id = instruction->result_id, .m_resultType = instruction->type_id});
				state.m_currentFunction = &state.m_module.m_functions.back();
				state.m_currentBlock = nullptr;
				return SPV_SUCCESS;
			}

			if (opcode == SpvOpFunctionEnd)
			{
				state.m_currentFunction = nullptr;
				state.m_currentBlock = nullptr;
				return SPV_SUCCESS;
			}

			if (opcode == SpvOpLabel)
			{
				if (!state.m_currentFunction)
					return SPV_ERROR_INVALID_BINARY;

				state.m_currentFunction->m_blocks.push_back(BasicBlock{.m_label = instruction->result_id});
				state.m_currentBlock = &state.m_currentFunction->m_blocks.back();
				return SPV_SUCCESS;
			}

			const Terminator terminator = ToTerminator(opcode);
			if (terminator != Terminator::None)
			{
				if (!state.m_currentBlock)
					return SPV_ERROR_INVALID_BINARY;

				state.m_currentBlock->m_terminator = terminator;
				FillTerminatorData(*state.m_currentBlock, opcode, *instruction);
				return SPV_SUCCESS;
			}

			Instruction converted = ToInstruction(*instruction);
			if (state.m_currentBlock)
				state.m_currentBlock->m_instructions.push_back(std::move(converted));
			else
				state.m_module.m_globalInstructions.push_back(std::move(converted));

			return SPV_SUCCESS;
		}
	} // namespace

	std::optional<Module> Parse(const std::vector<cct::UInt32>& code)
	{
		spv_context context = spvContextCreate(SPV_ENV_UNIVERSAL_1_5);
		ParseState state;

		spv_diagnostic diagnostic = nullptr;
		spv_result_t result = spvBinaryParse(context, &state, code.data(), code.size(), nullptr, OnInstruction, &diagnostic);

		if (result != SPV_SUCCESS)
		{
			if (diagnostic)
			{
				cct::Logger::Error("SPIR-V parse failed: {}", diagnostic->error);
				spvDiagnosticDestroy(diagnostic);
			}
			spvContextDestroy(context);
			return std::nullopt;
		}

		spvContextDestroy(context);
		return state.m_module;
	}
} // namespace see::spirv

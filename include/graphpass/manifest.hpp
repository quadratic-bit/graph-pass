#pragma once
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

#include <graphpass/ids.hpp>

std::string escape_manifest_field(llvm::StringRef field);

struct ModuleRecord {
	StableId module_id;

	llvm::StringRef graph_name;
	llvm::StringRef source_path;
};

struct FunctionRecord {
	StableId function_id;
	NodeId   cluster_node_id;

	llvm::StringRef function_name;
};

struct BasicBlockRecord {
	StableId bblock_id;
	StableId function_id;
	NodeId   cluster_node_id;
	StableId entry_instr_id;

	llvm::StringRef bblock_name;
};

struct InstructionRecord {
	StableId instruction_id;
	StableId bblock_id;
	NodeId   node_id;

	llvm::StringRef opcode_name;
	llvm::StringRef rendered_label;
};

struct SyntheticRecord {
	StableId synthetic_id;
	StableId owner_instruction_id;
	NodeId   node_id;

	llvm::StringRef rendered_label;
};

struct EdgeRecord {
	StableId edge_id;
	StableId from_instruction_id;
	StableId to_instruction_id;
	NodeId   from_node_id;
	NodeId   to_node_id;

	llvm::StringRef edge_kind;
};

struct CfgEdgeRecord {
	StableId edge_id;
	StableId from_bblock_id;
	StableId to_bblock_id;
	StableId from_instr_id;
	StableId to_instr_id;
	NodeId   from_node_id;
	NodeId   to_node_id;
};

class ManifestWriter {
public:
	explicit ManifestWriter(llvm::raw_ostream &out);

	void write(const ModuleRecord      &record);
	void write(const FunctionRecord    &record);
	void write(const BasicBlockRecord  &record);
	void write(const InstructionRecord &record);
	void write(const SyntheticRecord   &record);
	void write(const EdgeRecord        &record);
	void write(const CfgEdgeRecord     &record);

private:
	llvm::raw_ostream &out;
};

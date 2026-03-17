#pragma once
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <string>

#include "ids.hpp"

std::string escape_manifest_field(llvm::StringRef field);

void emit_manifest_header(
	llvm::raw_ostream &manifest,
	StableId module_id,
	llvm::StringRef graph_name,
	llvm::StringRef source_path
);

void emit_manifest_function(
	llvm::raw_ostream &manifest,
	StableId function_id,
	NodeId cluster_node_id,
	llvm::StringRef function_name
);

void emit_manifest_bblock(
	llvm::raw_ostream &manifest,
	StableId bblock_id,
	StableId function_id,
	NodeId cluster_node_id,
	StableId entry_instr_id,
	llvm::StringRef bblock_name
);

void emit_manifest_instruction(
	llvm::raw_ostream &manifest,
	StableId instruction_id,
	StableId bblock_id,
	NodeId node_id,
	llvm::StringRef opcode_name,
	llvm::StringRef rendered_label
);

void emit_manifest_synthetic(
	llvm::raw_ostream &manifest,
	StableId synthetic_id,
	StableId owner_instruction_id,
	NodeId node_id,
	llvm::StringRef rendered_label
);

void emit_manifest_edge(
	llvm::raw_ostream &manifest,
	StableId edge_id,
	llvm::StringRef edge_kind,
	StableId from_instruction_id,
	StableId to_instruction_id,
	NodeId from_node_id,
	NodeId to_node_id
);

void emit_manifest_cfg_edge(
	llvm::raw_ostream &manifest,
	StableId edge_id,
	StableId from_bblock_id,
	StableId to_bblock_id,
	StableId from_instr_id,
	StableId to_instr_id,
	NodeId from_node_id,
	NodeId to_node_id
);

void emit_manifest_cfg_edges(
	llvm::raw_ostream &manifest,
	llvm::BasicBlock &B,
	const StableIds &stable_ids,
	const RuntimeIds &runtime_ids
);

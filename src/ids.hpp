#pragma once
#include <llvm/IR/Module.h>

#include <unordered_map>

#include "common.hpp"

struct StableIds {
	StableId next_function_id = 1;
	StableId next_bblock_id = 1;
	StableId next_instruction_id = 1;

	std::unordered_map<const llvm::Function    *, StableId> function_ids;
	std::unordered_map<const llvm::BasicBlock  *, StableId> bblock_ids;
	std::unordered_map<const llvm::Instruction *, StableId> instruction_ids;

	StableId function_id(const llvm::Function *F) const {
		return function_ids.at(F);
	}

	StableId bblock_id(const llvm::BasicBlock *B) const {
		return bblock_ids.at(B);
	}

	StableId instruction_id(const llvm::Instruction *I) const {
		return instruction_ids.at(I);
	}
};
StableIds collect_stable_ids(llvm::Module &M);
StableId hash_module_id(llvm::StringRef text);

struct CFGEdgeRef {
	const llvm::BasicBlock *from;
	const llvm::BasicBlock *to;

	bool operator==(const CFGEdgeRef &other) const {
		return from == other.from && to == other.to;
	}
};

struct CFGEdgeRefHash {
	size_t operator()(const CFGEdgeRef &edge) const {
		return std::hash<const llvm::BasicBlock *>{}(edge.from) ^ (std::hash<const llvm::BasicBlock *>{}(edge.to) << 1);
	}
};

struct RuntimeIds {
	StableId module_id = 0;
	StableId next_cfg_edge_id = 1;

	std::unordered_map<CFGEdgeRef, StableId, CFGEdgeRefHash> cfg_edge_ids;

	StableId cfg_edge_id(const llvm::BasicBlock *from, const llvm::BasicBlock *to) const {
		return cfg_edge_ids.at({from, to});
	}
};

RuntimeIds collect_runtime_ids(llvm::Module &M);

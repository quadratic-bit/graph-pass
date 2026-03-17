#pragma once
#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Support/raw_ostream.h>

#include <string>

#include "ids.hpp"

void emit_graph_and_manifest(
	std::string filename,
	std::string source_path,
	llvm::Module &M,
	llvm::ModuleSlotTracker &slot_tracker,
	llvm::raw_ostream &dot,
	llvm::raw_ostream &manifest,
	const StableIds &stable_ids,
	const RuntimeIds &runtime_ids
);

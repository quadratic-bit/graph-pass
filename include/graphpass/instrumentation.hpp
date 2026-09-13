#pragma once
#include <llvm/IR/Module.h>

#include <graphpass/ids.hpp>

void instrument_runtime_logging(
	llvm::Module &M,
	const StableIds &stable_ids,
	const RuntimeIds &runtime_ids
);

#pragma once
#include <llvm/IR/Module.h>

#include <string>

struct GraphPassConfig {
	std::string artifact_name;
	std::string manifest_path;
	std::string source_path;
};

GraphPassConfig resolve_graphpass_config(const llvm::Module &M);

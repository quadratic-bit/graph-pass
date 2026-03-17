#include "ids.hpp"

using namespace llvm;

StableIds collect_stable_ids(Module &M) {
	StableIds ids;

	for (auto &F : M) {
		ids.function_ids.emplace(&F, ids.next_function_id++);

		for (auto &B : F) {
			ids.bblock_ids.emplace(&B, ids.next_bblock_id++);

			for (auto &I : B) {
				ids.instruction_ids.emplace(&I, ids.next_instruction_id++);
			}
		}
	}

	return ids;
}


StableId hash_module_id(StringRef text) {
	uint64_t hash = 14695981039346656037ull;

	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ull;
	}

	return hash ? hash : 1;
}

RuntimeIds collect_runtime_ids(Module &M) {
	RuntimeIds ids;
	StringRef module_key = M.getSourceFileName().empty()
		? M.getModuleIdentifier()
		: M.getSourceFileName();

	ids.module_id = hash_module_id(module_key);

	for (auto &F : M) {
		if (F.isDeclaration()) continue;

		for (auto &B : F) {
			Instruction *terminator = B.getTerminator();
			if (!terminator) continue;

			for (unsigned i = 0; i < terminator->getNumSuccessors(); ++i) {
				BasicBlock *successor = terminator->getSuccessor(i);
				CFGEdgeRef edge{&B, successor};

				if (ids.cfg_edge_ids.find(edge) == ids.cfg_edge_ids.end()) {
					ids.cfg_edge_ids.emplace(edge, ids.next_cfg_edge_id++);
				}
			}
		}
	}

	return ids;
}

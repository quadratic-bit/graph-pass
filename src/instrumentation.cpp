#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include "instrumentation.hpp"

using namespace llvm;

using std::vector;

struct RuntimeLoggerFns {
	FunctionCallee init;
	FunctionCallee bb;
	FunctionCallee edge;
	FunctionCallee call;
};

static RuntimeLoggerFns declare_runtime_logger_fns(Module &M) {
	LLVMContext &ctx = M.getContext();
	Type *void_ty = Type::getVoidTy(ctx);
	Type *i64_ty = Type::getInt64Ty(ctx);

	return {
		M.getOrInsertFunction("__graphpass_log_init", void_ty, i64_ty),
		M.getOrInsertFunction("__graphpass_log_bb",   void_ty, i64_ty),
		M.getOrInsertFunction("__graphpass_log_edge", void_ty, i64_ty),
		M.getOrInsertFunction("__graphpass_log_call", void_ty, i64_ty)
	};
}

static bool is_graphpass_runtime_function(const Function *F) {
	if (!F) return false;

	StringRef name = F->getName();
	return name == "__graphpass_log_init"
		|| name == "__graphpass_log_bb"
		|| name == "__graphpass_log_edge"
		|| name == "__graphpass_log_call"
		|| name == "__graphpass_ctor";
}

static Function *create_runtime_log_ctor(
	Module &M,
	FunctionCallee log_init,
	StableId module_id
) {
	LLVMContext &ctx = M.getContext();
	FunctionType *ctor_ty = FunctionType::get(Type::getVoidTy(ctx), false);
	Function *ctor = Function::Create(
		ctor_ty,
		GlobalValue::InternalLinkage,
		"__graphpass_ctor",
		M
	);

	BasicBlock *entry = BasicBlock::Create(ctx, "entry", ctor);
	IRBuilder<> builder(entry);

	builder.CreateCall(log_init, {builder.getInt64(module_id)});
	builder.CreateRetVoid();

	appendToGlobalCtors(M, ctor, 0);
	return ctor;
}

static void instrument_basic_block_entries(
	Function &F,
	const StableIds &stable_ids,
	FunctionCallee log_bb
) {
	for (auto &B : F) {
		auto insert_it = B.getFirstInsertionPt();
		if (insert_it == B.end()) continue;

		IRBuilder<> builder(&*insert_it);
		builder.CreateCall(log_bb, {builder.getInt64(stable_ids.bblock_id(&B))});
	}
}

static void instrument_call_sites(
	Function &F,
	const StableIds &stable_ids,
	FunctionCallee log_call
) {
	for (auto &B : F) {
		for (Instruction &I : make_early_inc_range(B)) {
			auto *CB = dyn_cast<CallBase>(&I);
			if (!CB) continue;

			if (Function *callee = CB->getCalledFunction()) {
				if (callee->isIntrinsic()) continue;
				if (is_graphpass_runtime_function(callee)) continue;
			}

			IRBuilder<> builder(CB);
			builder.CreateCall(log_call, {builder.getInt64(stable_ids.instruction_id(&I))});
		}
	}
}

static void instrument_cfg_edges(
	Function &F,
	const RuntimeIds &runtime_ids,
	FunctionCallee log_edge
) {
	vector<CFGEdgeRef> cfg_edges;

	for (auto &B : F) {
		Instruction *terminator = B.getTerminator();
		if (!terminator) continue;

		for (unsigned i = 0; i < terminator->getNumSuccessors(); ++i) {
			CFGEdgeRef edge{&B, terminator->getSuccessor(i)};

			if (std::find(cfg_edges.begin(), cfg_edges.end(), edge) == cfg_edges.end()) {
				cfg_edges.push_back(edge);
			}
		}
	}

	for (const CFGEdgeRef &edge : cfg_edges) {
		BasicBlock *from = const_cast<BasicBlock *>(edge.from);
		BasicBlock *to = const_cast<BasicBlock *>(edge.to);
		Instruction *terminator = from->getTerminator();
		if (!terminator) continue;

		BasicBlock *log_block = nullptr;

		if (terminator->getNumSuccessors() == 1) {
			log_block = from;
		} else {
			log_block = SplitEdge(from, to, nullptr, nullptr, nullptr, "__graphpass.edge");
			if (!log_block) continue;
		}

		IRBuilder<> builder(log_block->getTerminator());
		builder.CreateCall(log_edge, {builder.getInt64(runtime_ids.cfg_edge_id(edge.from, edge.to))});
	}
}

void instrument_runtime_logging(
	Module &M,
	const StableIds &stable_ids,
	const RuntimeIds &runtime_ids
) {
	RuntimeLoggerFns logger_fns = declare_runtime_logger_fns(M);
	Function *ctor = create_runtime_log_ctor(M, logger_fns.init, runtime_ids.module_id);

	for (auto &F : M) {
		if (F.isDeclaration()) continue;
		if (&F == ctor) continue;
		if (is_graphpass_runtime_function(&F)) continue;

		instrument_basic_block_entries(F, stable_ids, logger_fns.bb);
		instrument_call_sites(F, stable_ids, logger_fns.call);
		instrument_cfg_edges(F, runtime_ids, logger_fns.edge);
	}
}

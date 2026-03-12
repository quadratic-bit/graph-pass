#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/bit.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <system_error>

#define NODE_PREFIX "node"
#define FONTNAME "fontname=\"DepartureMono Nerd Font\""

#define CLR_INSTR "color=\"#4865a3\""
#define FILL_INSTR "fillcolor=\"#80a0e2\""

#define CLR_IMM "color=\"#3b7741\""
#define FILL_IMM "fillcolor=\"#75b279\""

#define CLR_DATA "color=\"#000000\""
#define CLR_SEQ "color=\"#d96a65\""

#define CLR_FUNC "bgcolor=\"#f0f0f0\""
#define CLR_BBLOCK "bgcolor=\"#e0e0e0\""

using namespace llvm;
using std::string;
using std::vector;

using NodeId = uintptr_t;
using StableId = uint64_t;

static constexpr uint64_t NODE_TAG_SHIFT = 60;

enum class NodeTag : uint64_t {
	Instruction     = 1,
	Synthetic       = 2,
	FunctionCluster = 3,
	BBlockCluster   = 4
};

static NodeId make_node_id(NodeTag tag, StableId id) {
	return (static_cast<uint64_t>(tag) << NODE_TAG_SHIFT) | id;
}

static NodeId make_instr_node_id(StableId id) {
	return make_node_id(NodeTag::Instruction, id);
}

static NodeId make_synthetic_node_id(StableId id) {
	return make_node_id(NodeTag::Synthetic, id);
}

static NodeId make_function_cluster_id(StableId id) {
	return make_node_id(NodeTag::FunctionCluster, id);
}

static NodeId make_bblock_cluster_id(StableId id) {
	return make_node_id(NodeTag::BBlockCluster, id);
}

struct StableIds {
	StableId next_function_id = 1;
	StableId next_bblock_id = 1;
	StableId next_instruction_id = 1;

	std::unordered_map<const Function    *, StableId> function_ids;
	std::unordered_map<const BasicBlock  *, StableId> bblock_ids;
	std::unordered_map<const Instruction *, StableId> instruction_ids;

	StableId function_id(const Function *F) const {
		return function_ids.at(F);
	}

	StableId bblock_id(const BasicBlock *B) const {
		return bblock_ids.at(B);
	}

	StableId instruction_id(const Instruction *I) const {
		return instruction_ids.at(I);
	}
};

static StableIds collect_stable_ids(Module &M) {
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

struct CFGEdgeRef {
	const BasicBlock *from;
	const BasicBlock *to;

	bool operator==(const CFGEdgeRef &other) const {
		return from == other.from && to == other.to;
	}
};

struct CFGEdgeRefHash {
	size_t operator()(const CFGEdgeRef &edge) const {
		return std::hash<const BasicBlock *>{}(edge.from) ^ (std::hash<const BasicBlock *>{}(edge.to) << 1);
	}
};

struct RuntimeIds {
	StableId module_id = 0;
	StableId next_cfg_edge_id = 1;

	std::unordered_map<CFGEdgeRef, StableId, CFGEdgeRefHash> cfg_edge_ids;

	StableId cfg_edge_id(const BasicBlock *from, const BasicBlock *to) const {
		return cfg_edge_ids.at({from, to});
	}
};

static StableId hash_module_id(StringRef text) {
	uint64_t hash = 14695981039346656037ull;

	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ull;
	}

	return hash ? hash : 1;
}

static RuntimeIds collect_runtime_ids(Module &M) {
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

static string escape_manifest_field(StringRef field) {
	string out;
	out.reserve(field.size());

	for (char c : field) {
		switch (c) {

		case '\\': out += "\\\\"; break;
		case '\t': out += "\\t";  break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		default:   out += c;      break;

		}
	}

	return out;
}

static void emit_manifest_header(
	raw_ostream &manifest,
	StableId module_id,
	StringRef graph_name,
	StringRef source_path
) {
	manifest << "graphpass-manifest\t1\n";
	manifest << "module\t"
		<< module_id << '\t'
		<< escape_manifest_field(graph_name) << '\t'
		<< escape_manifest_field(source_path) << '\n';
}

static void emit_manifest_function(
	raw_ostream &manifest,
	StableId function_id,
	NodeId cluster_node_id,
	StringRef function_name
) {
	manifest << "function\t"
		<< function_id << '\t'
		<< cluster_node_id << '\t'
		<< escape_manifest_field(function_name) << '\n';
}

static void emit_manifest_bblock(
	raw_ostream &manifest,
	StableId bblock_id,
	StableId function_id,
	NodeId cluster_node_id,
	StableId entry_instr_id,
	StringRef bblock_name
) {
	manifest << "bblock\t"
		<< bblock_id << '\t'
		<< function_id << '\t'
		<< cluster_node_id << '\t'
		<< entry_instr_id << '\t'
		<< escape_manifest_field(bblock_name) << '\n';
}

static void emit_manifest_instruction(
	raw_ostream &manifest,
	StableId instruction_id,
	StableId bblock_id,
	NodeId node_id,
	StringRef opcode_name,
	StringRef rendered_label
) {
	manifest << "instruction\t"
		<< instruction_id << '\t'
		<< bblock_id << '\t'
		<< node_id << '\t'
		<< escape_manifest_field(opcode_name) << '\t'
		<< escape_manifest_field(rendered_label) << '\n';
}

static void emit_manifest_edge(
	raw_ostream &manifest,
	StableId edge_id,
	StringRef edge_kind,
	StableId from_instruction_id,
	StableId to_instruction_id,
	NodeId from_node_id,
	NodeId to_node_id
) {
	manifest << "edge\t"
		<< edge_id << '\t'
		<< edge_kind << '\t'
		<< from_instruction_id << '\t'
		<< to_instruction_id << '\t'
		<< from_node_id << '\t'
		<< to_node_id << '\n';
}

static void emit_manifest_cfg_edge(
	raw_ostream &manifest,
	StableId edge_id,
	StableId from_bblock_id,
	StableId to_bblock_id,
	StableId from_instr_id,
	StableId to_instr_id,
	NodeId from_node_id,
	NodeId to_node_id
) {
	manifest << "cfg_edge\t"
		<< edge_id << '\t'
		<< from_bblock_id << '\t'
		<< to_bblock_id << '\t'
		<< from_instr_id << '\t'
		<< to_instr_id << '\t'
		<< from_node_id << '\t'
		<< to_node_id << '\n';
}

static void emit_manifest_cfg_edges(
	raw_ostream &manifest,
	BasicBlock &B,
	const StableIds &stable_ids,
	const RuntimeIds &runtime_ids
) {
	Instruction *terminator = B.getTerminator();
	if (!terminator) return;

	StableId from_bblock_id = stable_ids.bblock_id(&B);
	StableId from_instr_id = stable_ids.instruction_id(terminator);
	NodeId from_node_id = make_instr_node_id(from_instr_id);

	for (unsigned i = 0; i < terminator->getNumSuccessors(); ++i) {
		BasicBlock *successor = terminator->getSuccessor(i);
		StableId to_bblock_id = stable_ids.bblock_id(successor);
		StableId to_instr_id = stable_ids.instruction_id(&successor->front());
		NodeId to_node_id = make_instr_node_id(to_instr_id);

		emit_manifest_cfg_edge(
			manifest,
			runtime_ids.cfg_edge_id(&B, successor),
			from_bblock_id,
			to_bblock_id,
			from_instr_id,
			to_instr_id,
			from_node_id,
			to_node_id
		);
	}
}

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

static void instrument_runtime_logging(
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

static void emit_instr_node(NodeId nodeId, StringRef label) {
	outs() << "\t\t" NODE_PREFIX << nodeId
		<< " [label=\"" << label
		<< "\",style=filled," CLR_INSTR "," FILL_INSTR "," FONTNAME "];\n";
}

static void emit_synthetic_node(NodeId nodeId, StringRef label) {
	outs() << "\t\t" NODE_PREFIX << nodeId
		<< " [label=\"" << label
		<< "\",style=filled," CLR_IMM "," FILL_IMM "," FONTNAME "];\n";
}

static void emit_data_edge(NodeId from, NodeId to, const string &label) {
	outs() << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
		<< " [label=\"" << label
		<< "\",style=dashed," CLR_DATA "," FONTNAME "]\n";
}

static void emit_sequence_edge(NodeId from, NodeId to) {
	outs() << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
		<< " [" CLR_SEQ "]\n";
}

static void emit_block_edge(NodeId from, NodeId to) {
	outs() << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
		<< " [style=solid,penwidth=4," CLR_SEQ "," FONTNAME "]\n";
}

static void emit_function_cluster_begin(Function &F, StableId function_id) {
	NodeId cluster_id = make_function_cluster_id(function_id);
	outs() << "\tsubgraph cluster_" << cluster_id << " {\n\t\tlabel=\"" << F.getName() << "\"\n\t\t" FONTNAME "\n\t\t" CLR_FUNC "\n";
}

static void emit_bblock_cluster_begin(BasicBlock &B, StableId bblock_id) {
	NodeId cluster_id = make_bblock_cluster_id(bblock_id);
	outs() << "\t\tsubgraph cluster_" << cluster_id << " {\n\t\t\tlabel=\"" << B.getName() << "\"\n\t\t\t" CLR_BBLOCK "\n";
}

static void emit_cluster_end(unsigned short indentation) {
	for (unsigned short i = 0; i < indentation; ++i) {
		outs() << "\t";
	}
	outs() << "}\n";
}

static string format_call_args(const vector<string> &args) {
	string result = "(";
	for (size_t idx = 0; idx < args.size(); ++idx) {
		if (idx != 0) result += ", ";
		result += args[idx];
	}
	result += ")";
	return result;
}

static void strip_call_callee(Instruction &I, decltype(I.operands()) &operand_range, string &instruction_label) {
	auto &callee_operand = *std::prev(operand_range.end());
	instruction_label += " ";
	instruction_label += callee_operand.get()->getName();
	operand_range = drop_end(I.operands());
}

static void process_operand(
		Use &Op,
		Instruction &I,
		StableId instr_id,
		NodeId instr_node_id,
		bool is_call,
		ModuleSlotTracker &slot_tracker,
		raw_ostream &manifest,
		const StableIds &stable_ids,
		StableId &next_edge_id,
		StableId &next_synthetic_node_id,
		vector<string> &call_args
) {
	Value *operand_value = Op.get();
	Instruction *source_instr = dyn_cast<Instruction>(Op);
	BasicBlock  *source_block = dyn_cast<BasicBlock>(Op);

	int slot = slot_tracker.getLocalSlot(Op);
	bool is_immediate = slot == -1;
	string operand_label = is_immediate ? "" : "%" + std::to_string(slot);

	if (source_block) {
		StableId target_instr_id = stable_ids.instruction_id(&source_block->front());
		NodeId target_node_id = make_instr_node_id(target_instr_id);

		emit_block_edge(instr_node_id, target_node_id);
		emit_manifest_edge(
			manifest,
			next_edge_id++,
			"block",
			instr_id,
			target_instr_id,
			instr_node_id,
			target_node_id
		);
		return;
	}

	if (source_instr) {
		StableId source_instr_id = stable_ids.instruction_id(source_instr);
		NodeId source_node_id = make_instr_node_id(source_instr_id);

		call_args.push_back(operand_label);
		emit_data_edge(source_node_id, instr_node_id, operand_label);
		emit_manifest_edge(
			manifest,
			next_edge_id++,
			"data",
			source_instr_id,
			instr_id,
			source_node_id,
			instr_node_id
		);
		return;
	}

	if (!is_immediate) return;

	// syntetic value

	NodeId operand_node_id = make_synthetic_node_id(next_synthetic_node_id++);
	string operandNodeLabel = operand_value->getName().empty()
		? operand_value->getNameOrAsOperand()
		: operand_value->getName().str();

	if (is_call) {
		call_args.push_back(operandNodeLabel);
		return;
	}

	emit_synthetic_node(operand_node_id, operandNodeLabel);
	emit_data_edge(operand_node_id, instr_node_id, operand_label);
}

static NodeId emit_instruction(
	Instruction &I,
	StableId bblock_id,
	ModuleSlotTracker &slot_tracker,
	raw_ostream &manifest,
	const StableIds &stable_ids,
	StableId &next_edge_id,
	StableId &next_synthetic_node_id
) {
	auto operand_range = I.operands();
	auto instruction_label = string(I.getOpcodeName());
	bool is_call = strcmp(I.getOpcodeName(), "call") == 0;
	StableId instr_id = stable_ids.instruction_id(&I);
	NodeId instr_node_id = make_instr_node_id(instr_id);
	auto next_instr = I.getNextNode();
	NodeId next_node_id = next_instr
		? make_instr_node_id(stable_ids.instruction_id(next_instr))
		: 0;

	if (is_call && !operand_range.empty())
		strip_call_callee(I, operand_range, instruction_label);

	vector<string> call_args;

	for (auto &Op : operand_range) {
		process_operand(
			Op,
			I,
			instr_id,
			instr_node_id,
			is_call,
			slot_tracker,
			manifest,
			stable_ids,
			next_edge_id,
			next_synthetic_node_id,
			call_args
		);
	}

	if (is_call) instruction_label += format_call_args(call_args);

	emit_instr_node(instr_node_id, instruction_label);
	emit_manifest_instruction(
		manifest,
		instr_id,
		bblock_id,
		instr_node_id,
		I.getOpcodeName(),
		instruction_label
	);
	if (next_node_id) {
		StableId next_instr_id = stable_ids.instruction_id(next_instr);

		emit_sequence_edge(instr_node_id, next_node_id);
		emit_manifest_edge(
			manifest,
			next_edge_id++,
			"seq",
			instr_id,
			next_instr_id,
			instr_node_id,
			next_node_id
		);
	}
	return instr_node_id;
}

static void emit_basic_block_cluster(BasicBlock &B, StableId bblock_id, const vector<NodeId> &bblock_node_ids) {
	emit_bblock_cluster_begin(B, bblock_id);
	for (NodeId node_id : bblock_node_ids) {
		outs() << "\t\t\t" NODE_PREFIX << node_id << '\n';
	}
	emit_cluster_end(2);
}

struct GraphPass : public PassInfoMixin<GraphPass> {
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
		auto source_path = M.getSourceFileName();
		auto filename = source_path.substr(source_path.find_last_of('/') + 1);
		std::replace(filename.begin(), filename.end(), '.', '_');

		ModuleSlotTracker slot_tracker(&M);

		StableIds stable_ids = collect_stable_ids(M);
		RuntimeIds runtime_ids = collect_runtime_ids(M);
		string manifest_path = filename + ".manifest.tsv";
		std::error_code manifest_error;
		raw_fd_ostream manifest(manifest_path, manifest_error, sys::fs::OF_Text);

		if (manifest_error) {
			errs() << "GraphPass: failed to open manifest file '"
			       << manifest_path << "': "
			       << manifest_error.message() << '\n';
			return PreservedAnalyses::all();
		}
		StableId next_edge_id = 1;
		StableId next_synthetic_node_id = 1;

		outs() << "digraph " << filename << " {\n\trankdir=TB;\n\tdpi=300\n\tnode [shape=box];\n\tlabel=\"" << source_path << "\"\n\t" FONTNAME "\n";
		emit_manifest_header(manifest, runtime_ids.module_id, filename, source_path);
		for (auto &F : M) {
			slot_tracker.incorporateFunction(F);
			outs() << '\n';

			StableId function_id = stable_ids.function_id(&F);
			emit_manifest_function(
				manifest,
				function_id,
				make_function_cluster_id(function_id),
				F.getName()
			);
			emit_function_cluster_begin(F, function_id);
			for (auto &B : F) {
				StableId bblock_id = stable_ids.bblock_id(&B);
				StableId entry_instr_id = B.empty()
					? 0
					: stable_ids.instruction_id(&B.front());

				emit_manifest_bblock(
					manifest,
					bblock_id,
					function_id,
					make_bblock_cluster_id(bblock_id),
					entry_instr_id,
					B.getName()
				);
				emit_manifest_cfg_edges(manifest, B, stable_ids, runtime_ids);

				vector<NodeId> bblock_node_ids;
				for (auto &I : B) {
					NodeId instr_node_id = emit_instruction(
						I,
						bblock_id,
						slot_tracker,
						manifest,
						stable_ids,
						next_edge_id,
						next_synthetic_node_id
					);
					bblock_node_ids.push_back(instr_node_id);
				}

				emit_basic_block_cluster(B, stable_ids.bblock_id(&B), bblock_node_ids);
			}
			emit_cluster_end(1);
		}
		outs() << "}\n";
		manifest.close();
		if (manifest.has_error()) {
			errs() << "GraphPass: failed while writing manifest file '"
			       << manifest_path << "'\n";
			manifest.clear_error();
		}
		instrument_runtime_logging(M, stable_ids, runtime_ids);
		return PreservedAnalyses::none();
	};
};

PassPluginLibraryInfo getPassPluginInfo() {
	const auto callback = [](PassBuilder &PB) {
		PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM, auto, auto) {
			MPM.addPass(GraphPass{});
			return true;
		});
	};

	return {LLVM_PLUGIN_API_VERSION, "GraphPass", "0.0.1", callback};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
	return getPassPluginInfo();
}

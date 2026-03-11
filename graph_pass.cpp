#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/bit.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

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
		NodeId instr_node_id,
		bool is_call,
		ModuleSlotTracker &slot_tracker,
		const StableIds &stable_ids,
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
		NodeId target_node_id = make_instr_node_id(stable_ids.instruction_id(&source_block->front()));
		emit_block_edge(instr_node_id, target_node_id);
		return;
	}

	if (source_instr) {
		NodeId source_node_id = make_instr_node_id(stable_ids.instruction_id(source_instr));
		call_args.push_back(operand_label);
		emit_data_edge(source_node_id, instr_node_id, operand_label);
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
	ModuleSlotTracker &slot_tracker,
	const StableIds &stable_ids,
	StableId &next_synthetic_node_id
) {
	auto operand_range = I.operands();
	auto instruction_label = string(I.getOpcodeName());
	bool is_call = strcmp(I.getOpcodeName(), "call") == 0;
	NodeId instr_node_id = make_instr_node_id(stable_ids.instruction_id(&I));
	auto next_instr = I.getNextNode();
	NodeId next_node_id = next_instr
		? make_instr_node_id(stable_ids.instruction_id(next_instr))
		: 0;

	if (is_call && !operand_range.empty())
		strip_call_callee(I, operand_range, instruction_label);

	vector<string> call_args;

	for (auto &Op : operand_range) {
		process_operand(Op, I, instr_node_id, is_call, slot_tracker, stable_ids, next_synthetic_node_id, call_args);
	}

	if (is_call) instruction_label += format_call_args(call_args);

	emit_instr_node(instr_node_id, instruction_label);
	if (next_node_id) {
		emit_sequence_edge(instr_node_id, next_node_id);
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
		StableId next_synthetic_node_id = 1;

		outs() << "digraph " << filename << " {\n\trankdir=TB;\n\tdpi=300\n\tnode [shape=box];\n\tlabel=\"" << source_path << "\"\n\t" FONTNAME "\n";
		for (auto &F : M) {
			slot_tracker.incorporateFunction(F);
			outs() << '\n';

			emit_function_cluster_begin(F, stable_ids.function_id(&F));
			for (auto &B : F) {
				vector<NodeId> bblock_node_ids;

				for (auto &I : B) {
					NodeId instr_node_id = emit_instruction(I, slot_tracker, stable_ids, next_synthetic_node_id);
					bblock_node_ids.push_back(instr_node_id);
				}

				emit_basic_block_cluster(B, stable_ids.bblock_id(&B), bblock_node_ids);
			}
			emit_cluster_end(1);
		}
		outs() << "}\n";
		return PreservedAnalyses::all();
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

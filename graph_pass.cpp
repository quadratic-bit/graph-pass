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

static void emit_instr_node(NodeId nodeId, StringRef label) {
	outs() << "\t\tn" << nodeId
		<< " [label=\"" << label
		<< "\",style=filled," CLR_INSTR "," FILL_INSTR "," FONTNAME "];\n";
}

static void emit_synthetic_node(NodeId nodeId, StringRef label) {
	outs() << "\t\tn" << nodeId
		<< " [label=\"" << label
		<< "\",style=filled," CLR_IMM "," FILL_IMM "," FONTNAME "];\n";
}

static void emit_data_edge(NodeId from, NodeId to, const string &label) {
	outs() << "\t\tn" << from << " -> n" << to
		<< " [label=\"" << label
		<< "\",style=dashed," CLR_DATA "," FONTNAME "]\n";
}

static void emit_sequence_edge(NodeId from, NodeId to) {
	outs() << "\t\tn" << from << " -> n" << to
		<< " [" CLR_SEQ "]\n";
}

static void emit_block_edge(NodeId from, NodeId to) {
	outs() << "\t\tn" << from << " -> n" << to
		<< " [style=solid,penwidth=4," CLR_SEQ "," FONTNAME "]\n";
}

static void emit_function_cluster_begin(Function &F) {
	outs() << "\tsubgraph cluster_" << &F << " {\n\t\tlabel=\"" << F.getName() << "\"\n\t\t" FONTNAME "\n\t\t" CLR_FUNC "\n";
}

static void emit_bblock_cluster_begin(BasicBlock &B) {
	NodeId bblock_cluster_id = reinterpret_cast<NodeId>(&B);
	auto block_id_str = std::to_string(bblock_cluster_id);
	outs() << "\t\tsubgraph cluster_" << block_id_str << " {\n\t\t\tlabel=\"" << B.getName() << "\"\n\t\t\t" CLR_BBLOCK "\n";
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
		NodeId &next_synthetic_node_id,
		vector<string> &call_args
) {
	Value *operand_value = Op.get();
	Instruction *source_instr = dyn_cast<Instruction>(Op);
	BasicBlock  *source_block = dyn_cast<BasicBlock>(Op);

	int slot = slot_tracker.getLocalSlot(Op);
	bool is_immediate = slot == -1;
	string operand_label = is_immediate ? "" : "%" + std::to_string(slot);

	if (source_block) {
		emit_block_edge(instr_node_id, reinterpret_cast<NodeId>(&source_block->front()));
		return;
	}

	if (source_instr) {
		call_args.push_back(operand_label);
		emit_data_edge(reinterpret_cast<NodeId>(source_instr), instr_node_id, operand_label);
		return;
	}

	if (!is_immediate) return;

	// syntetic value

	NodeId operand_node_id = next_synthetic_node_id++;
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
	NodeId &next_synthetic_node_id
) {
	auto operand_range = I.operands();
	auto instruction_label = string(I.getOpcodeName());
	bool is_call = strcmp(I.getOpcodeName(), "call") == 0;
	NodeId instr_node_id = reinterpret_cast<NodeId>(&I);
	auto next_instr = I.getNextNode();
	NodeId next_node_id = next_instr ? reinterpret_cast<NodeId>(next_instr) : 0;

	if (is_call && !operand_range.empty())
		strip_call_callee(I, operand_range, instruction_label);

	vector<string> call_args;

	for (auto &Op : operand_range) {
		process_operand(Op, I, instr_node_id, is_call, slot_tracker, next_synthetic_node_id, call_args);
	}

	if (is_call) instruction_label += format_call_args(call_args);

	emit_instr_node(instr_node_id, instruction_label);
	if (next_node_id) {
		emit_sequence_edge(instr_node_id, next_node_id);
	}
	return instr_node_id;
}

static void emit_basic_block_cluster(BasicBlock &B, const vector<NodeId> &bblock_node_ids) {
	emit_bblock_cluster_begin(B);
	for (NodeId node_id : bblock_node_ids) {
		outs() << "\t\t\tn" << node_id << '\n';
	}
	emit_cluster_end(2);
}

struct GraphPass : public PassInfoMixin<GraphPass> {
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
		auto source_path = M.getSourceFileName();
		auto filename = source_path.substr(source_path.find_last_of('/') + 1);
		std::replace(filename.begin(), filename.end(), '.', '_');
		ModuleSlotTracker slot_tracker(&M);

		NodeId next_synthetic_node_id = 1;

		outs() << "digraph " << filename << " {\n\trankdir=TB;\n\tdpi=300\n\tnode [shape=box];\n\tlabel=\"" << source_path << "\"\n\t" FONTNAME "\n";
		for (auto &F : M) {
			slot_tracker.incorporateFunction(F);
			outs() << '\n';

			emit_function_cluster_begin(F);
			for (auto &B : F) {
				vector<NodeId> bblock_node_ids;

				for (auto &I : B) {
					NodeId instr_node_id = emit_instruction(I, slot_tracker, next_synthetic_node_id);
					bblock_node_ids.push_back(instr_node_id);
				}

				emit_basic_block_cluster(B, bblock_node_ids);
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
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
	return getPassPluginInfo();
}

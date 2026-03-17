#include <llvm/IR/ModuleSlotTracker.h>

#include "render.hpp"
#include "manifest.hpp"

using namespace llvm;
using std::string;
using std::vector;

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

	StableId synthetic_id = next_synthetic_node_id++;
	NodeId operand_node_id = make_synthetic_node_id(synthetic_id);
	string operandNodeLabel = operand_value->getName().empty()
		? operand_value->getNameOrAsOperand()
		: operand_value->getName().str();

	if (is_call) {
		call_args.push_back(operandNodeLabel);
		return;
	}

	emit_manifest_synthetic(
		manifest,
		synthetic_id,
		instr_id,
		operand_node_id,
		operandNodeLabel
	);
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

void emit_graph_and_manifest(
		string filename,
		string source_path,
		llvm::Module &M,
		llvm::ModuleSlotTracker &slot_tracker,
		llvm::raw_ostream &dot,
		llvm::raw_ostream &manifest,
		const StableIds &stable_ids,
		const RuntimeIds &runtime_ids
) {
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
}

#include <graphpass/render.hpp>
#include <graphpass/manifest.hpp>

#include <llvm/IR/ModuleSlotTracker.h>

using namespace llvm;
using std::string;
using std::vector;

struct RenderContext {
	ModuleSlotTracker &slot_tracker;
	raw_ostream       &dot;
	ManifestWriter    &manifest;

	const StableIds  &stable_ids;
	const RuntimeIds &runtime_ids;

	StableId next_edge_id           = 1;
	StableId next_synthetic_node_id = 1;
};

static void emit_legend(RenderContext &ctx) {
	ctx.dot << R"(
	subgraph cluster_legend {
		label="Legend";
		style="rounded";
		color="#b0b0b0";
)";

	ctx.dot << "\t\t" << FONTNAME << ";\n";

	ctx.dot << "\t\t{ rank=same;\n"
	        << "\t\t\tlegend_instr "
	        << "[label=\"instruction\",style=filled,"
	        << CLR_INSTR << "," << FILL_INSTR << "," << FONTNAME << "];\n"
	        << "\t\t}\n";

	ctx.dot << "\t\t{ rank=same;\n"
	        << "\t\t\tlegend_synthetic "
	        << "[label=\"immediate / synthetic\",style=filled,"
	        << CLR_IMM << "," << FILL_IMM << "," << FONTNAME << "];\n"
	        << "\t\t}\n";

	ctx.dot << R"(
		{ rank=same;
			legend_data_from [label="",shape=point,width=0.05];
			legend_data_to   [label="",shape=point,width=0.05];
		}
)";
	ctx.dot << "\t\tlegend_data_from -> legend_data_to "
	        << "[label=\"data dependency\",style=dashed,"
	        << CLR_DATA << "," << FONTNAME << "];\n";

	ctx.dot << R"(
		{ rank=same;
			legend_seq_from [label="",shape=point,width=0.05];
			legend_seq_to   [label="",shape=point,width=0.05];
		}
)";
	ctx.dot << "\t\tlegend_seq_from -> legend_seq_to "
	        << "[label=\"instruction sequence\","
	        << CLR_SEQ << "," << FONTNAME << "];\n";

	ctx.dot << R"(
		{ rank=same;
			legend_cfg_from [label="",shape=point,width=0.05];
			legend_cfg_to   [label="",shape=point,width=0.05];
		}
)";
	ctx.dot << "\t\tlegend_cfg_from -> legend_cfg_to "
	        << "[label=\"control-flow edge\",penwidth=4,"
	        << CLR_SEQ << "," << FONTNAME << "];\n";

	ctx.dot << R"(
		legend_instr -> legend_synthetic [style=invis,weight=100];
		legend_synthetic -> legend_data_from [style=invis,weight=100];
		legend_data_from -> legend_seq_from [style=invis,weight=100];
		legend_seq_from -> legend_cfg_from [style=invis,weight=100];
	}
)";
}

static void emit_instr_node(raw_ostream &dot, NodeId node_id, StringRef label) {
	dot << "\t\t" NODE_PREFIX << node_id
	    << " [label=\"" << label
	    << "\",style=filled,"
	    << CLR_INSTR <<  ","
	    << FILL_INSTR <<  ","
	    << FONTNAME << "];\n";
}

static void emit_synthetic_node(raw_ostream &dot, NodeId node_id, StringRef label) {
	dot << "\t\t" NODE_PREFIX << node_id
	    << " [label=\"" << label
	    << "\",style=filled," << CLR_IMM << "," << FILL_IMM << "," << FONTNAME << "];\n";
}

static void emit_data_edge(raw_ostream &dot, NodeId from, NodeId to, const string &label) {
	dot << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
	    << " [label=\"" << label
	    << "\",style=dashed," << CLR_DATA << "," << FONTNAME << "]\n";
}

static void emit_sequence_edge(raw_ostream &dot, NodeId from, NodeId to) {
	dot << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
	    << " [" << CLR_SEQ << "]\n";
}

static void emit_block_edge(raw_ostream &dot, NodeId from, NodeId to) {
	dot << "\t\t" NODE_PREFIX << from << " -> " NODE_PREFIX << to
	    << " [style=solid,penwidth=4," << CLR_SEQ << "," << FONTNAME << "]\n";
}

static void emit_function_cluster_begin(RenderContext &ctx, Function &F, StableId function_id) {
	NodeId cluster_id = make_function_cluster_id(function_id);
	ctx.dot << "\tsubgraph cluster_" << cluster_id << " {\n\t\tlabel=\""
	        << F.getName() << "\"\n\t\t" << FONTNAME << "\n\t\t" << CLR_FUNC << "\n";
}

static void emit_bblock_cluster_begin(RenderContext &ctx, BasicBlock &B, StableId bblock_id) {
	NodeId cluster_id = make_bblock_cluster_id(bblock_id);
	ctx.dot << "\t\tsubgraph cluster_" << cluster_id << " {\n\t\t\tlabel=\""
	        << B.getName() << "\"\n\t\t\t" << CLR_BBLOCK << "\n";
}

static void emit_cluster_end(RenderContext &ctx, unsigned short indentation) {
	for (unsigned short i = 0; i < indentation; ++i) {
		ctx.dot << "\t";
	}
	ctx.dot << "}\n";
}

static void emit_cfg_edges(RenderContext &ctx, BasicBlock &B) {
	Instruction *terminator = B.getTerminator();
	if (!terminator) return;

	StableId from_bblock_id = ctx.stable_ids.bblock_id(&B);
	StableId from_instr_id  = ctx.stable_ids.instruction_id(terminator);
	NodeId   from_node_id   = make_instr_node_id(from_instr_id);

	for (unsigned i = 0; i < terminator->getNumSuccessors(); ++i) {
		BasicBlock *successor = terminator->getSuccessor(i);

		StableId to_bblock_id = ctx.stable_ids.bblock_id(successor);
		StableId to_instr_id  = ctx.stable_ids.instruction_id(&successor->front());
		NodeId to_node_id     = make_instr_node_id(to_instr_id);

		ctx.manifest.write(CfgEdgeRecord{
			ctx.runtime_ids.cfg_edge_id(&B, successor),
			from_bblock_id, to_bblock_id,
			from_instr_id,  to_instr_id,
			from_node_id,   to_node_id,
		});
	}
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
	RenderContext &ctx,
	Use &Op,
	StableId instr_id,
	NodeId instr_node_id,
	bool is_call,
	vector<string> &call_args
) {
	Value *operand_value = Op.get();
	Instruction *source_instr = dyn_cast<Instruction>(Op);
	BasicBlock  *source_block = dyn_cast<BasicBlock>(Op);

	int slot = ctx.slot_tracker.getLocalSlot(Op);
	bool is_immediate = slot == -1;
	string operand_label = is_immediate ? "" : "%" + std::to_string(slot);

	if (source_block) {
		StableId target_instr_id = ctx.stable_ids.instruction_id(&source_block->front());
		NodeId target_node_id = make_instr_node_id(target_instr_id);

		emit_block_edge(ctx.dot, instr_node_id, target_node_id);
		ctx.manifest.write(EdgeRecord{
			ctx.next_edge_id++,
			instr_id,
			target_instr_id,
			instr_node_id,
			target_node_id,
			"block",
		});
		return;
	}

	if (source_instr) {
		StableId source_instr_id = ctx.stable_ids.instruction_id(source_instr);
		NodeId source_node_id = make_instr_node_id(source_instr_id);

		call_args.push_back(operand_label);
		emit_data_edge(ctx.dot, source_node_id, instr_node_id, operand_label);
		ctx.manifest.write(EdgeRecord{
			ctx.next_edge_id++,
			source_instr_id,
			instr_id,
			source_node_id,
			instr_node_id,
			"data",
		});
		return;
	}

	if (!is_immediate) return;

	// syntetic value

	StableId synthetic_id = ctx.next_synthetic_node_id++;
	NodeId operand_node_id = make_synthetic_node_id(synthetic_id);
	string operand_node_label = operand_value->getName().empty()
		? operand_value->getNameOrAsOperand()
		: operand_value->getName().str();

	if (is_call) {
		call_args.push_back(operand_node_label);
		return;
	}

	ctx.manifest.write(SyntheticRecord{
		synthetic_id,
		instr_id,
		operand_node_id,
		operand_node_label,
	});
	emit_synthetic_node(ctx.dot, operand_node_id, operand_node_label);
	emit_data_edge(ctx.dot, operand_node_id, instr_node_id, operand_label);
}

static NodeId emit_instruction(RenderContext &ctx, Instruction &I, StableId bblock_id) {
	auto operand_range = I.operands();
	auto instruction_label = string(I.getOpcodeName());
	bool is_call = strcmp(I.getOpcodeName(), "call") == 0;
	StableId instr_id = ctx.stable_ids.instruction_id(&I);
	NodeId instr_node_id = make_instr_node_id(instr_id);
	auto next_instr = I.getNextNode();
	NodeId next_node_id = next_instr
		? make_instr_node_id(ctx.stable_ids.instruction_id(next_instr))
		: 0;

	if (is_call && !operand_range.empty())
		strip_call_callee(I, operand_range, instruction_label);

	vector<string> call_args;

	for (auto &Op : operand_range) {
		process_operand(ctx, Op, instr_id, instr_node_id, is_call, call_args);
	}

	if (is_call) instruction_label += format_call_args(call_args);

	emit_instr_node(ctx.dot, instr_node_id, instruction_label);
	ctx.manifest.write(InstructionRecord{
		instr_id,
		bblock_id,
		instr_node_id,
		I.getOpcodeName(),
		instruction_label,
	});
	if (next_node_id) {
		StableId next_instr_id = ctx.stable_ids.instruction_id(next_instr);

		emit_sequence_edge(ctx.dot, instr_node_id, next_node_id);
		ctx.manifest.write(EdgeRecord{
			ctx.next_edge_id++,
			instr_id,
			next_instr_id,
			instr_node_id,
			next_node_id,
			"seq",
		});
	}
	return instr_node_id;
}

static void emit_basic_block_cluster(RenderContext &ctx, BasicBlock &B, StableId bblock_id, const vector<NodeId> &bblock_node_ids) {
	emit_bblock_cluster_begin(ctx, B, bblock_id);
	for (NodeId node_id : bblock_node_ids) {
		ctx.dot << "\t\t\t" NODE_PREFIX << node_id << '\n';
	}
	emit_cluster_end(ctx, 2);
}

void emit_graph_and_manifest(
	string filename,
	string source_path,
	llvm::Module &M,
	llvm::ModuleSlotTracker &slot_tracker,
	llvm::raw_ostream &dot,
	llvm::raw_ostream &manifest,
	const StableIds  &stable_ids,
	const RuntimeIds &runtime_ids
) {
	ManifestWriter manifest_writer(manifest);

	RenderContext ctx{
		slot_tracker,
		dot,
		manifest_writer,
		stable_ids,
		runtime_ids,
	};

	ctx.dot << "digraph " << filename
	        << " {\n\trankdir=TB;\n\tdpi=300\n\tnode [shape=box];\n\tlabel=\"" << source_path
	        << "\"\n\t" << FONTNAME << "\n";

	ctx.manifest.write(ModuleRecord{ctx.runtime_ids.module_id, filename, source_path});

	for (auto &F : M) {
		ctx.slot_tracker.incorporateFunction(F);
		ctx.dot << '\n';

		StableId function_id = ctx.stable_ids.function_id(&F);
		ctx.manifest.write(FunctionRecord{
			function_id,
			make_function_cluster_id(function_id),
			F.getName(),
		});

		emit_function_cluster_begin(ctx, F, function_id);
		for (auto &B : F) {
			StableId bblock_id = ctx.stable_ids.bblock_id(&B);
			StableId entry_instr_id = B.empty()
				? 0
				: ctx.stable_ids.instruction_id(&B.front());

			ctx.manifest.write(BasicBlockRecord{
				bblock_id,
				function_id,
				make_bblock_cluster_id(bblock_id),
				entry_instr_id,
				B.getName(),
			});
			emit_cfg_edges(ctx, B);

			vector<NodeId> bblock_node_ids;
			for (auto &I : B) {
				NodeId instr_node_id = emit_instruction(ctx, I, bblock_id);
				bblock_node_ids.push_back(instr_node_id);
			}

			emit_basic_block_cluster(ctx, B, ctx.stable_ids.bblock_id(&B), bblock_node_ids);
		}
		emit_cluster_end(ctx, 1);
	}
	emit_legend(ctx);
	ctx.dot << "}\n";
}

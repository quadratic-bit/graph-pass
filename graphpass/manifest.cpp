#include "manifest.hpp"

using namespace llvm;

using std::string;

string escape_manifest_field(StringRef field) {
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

void emit_manifest_header(
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

void emit_manifest_function(
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

void emit_manifest_bblock(
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

void emit_manifest_instruction(
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

void emit_manifest_synthetic(
	raw_ostream &manifest,
	StableId synthetic_id,
	StableId owner_instruction_id,
	NodeId node_id,
	StringRef rendered_label
) {
	manifest << "synthetic\t"
		<< synthetic_id << '\t'
		<< owner_instruction_id << '\t'
		<< node_id << '\t'
		<< escape_manifest_field(rendered_label) << '\n';
}

void emit_manifest_edge(
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

void emit_manifest_cfg_edge(
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

void emit_manifest_cfg_edges(
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

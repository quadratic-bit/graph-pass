#include <graphpass/manifest.hpp>

#include <string>

using namespace llvm;

namespace {
	std::string escape_field(StringRef field) {
		std::string out;
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
}

ManifestWriter::ManifestWriter(raw_ostream &out) : out(out) {
	out << "graphpass-manifest\t1\n";
}

void ManifestWriter::write(const ModuleRecord &record) {
	out << "module"                         << '\t'
	    << record.module_id                 << '\t'
	    << escape_field(record.graph_name)  << '\t'
	    << escape_field(record.source_path) << '\n';
}

void ManifestWriter::write(const FunctionRecord &record) {
	out << "function"                         << '\t'
	    << record.function_id                 << '\t'
	    << record.cluster_node_id             << '\t'
	    << escape_field(record.function_name) << '\n';
}

void ManifestWriter::write(const BasicBlockRecord &record) {
	out << "bblock"                         << '\t'
	    << record.bblock_id                 << '\t'
	    << record.function_id               << '\t'
	    << record.cluster_node_id           << '\t'
	    << record.entry_instr_id            << '\t'
	    << escape_field(record.bblock_name) << '\n';
}

void ManifestWriter::write(const InstructionRecord &record) {
	out << "instruction"                       << '\t'
	    << record.instruction_id               << '\t'
	    << record.bblock_id                    << '\t'
	    << record.node_id                      << '\t'
	    << escape_field(record.opcode_name)    << '\t'
	    << escape_field(record.rendered_label) << '\n';
}

void ManifestWriter::write(const SyntheticRecord &record) {
	out << "synthetic"                         << '\t'
	    << record.synthetic_id                 << '\t'
	    << record.owner_instruction_id         << '\t'
	    << record.node_id                      << '\t'
	    << escape_field(record.rendered_label) << '\n';
}

void ManifestWriter::write(const EdgeRecord &record) {
	out << "edge"                     << '\t'
	    << record.edge_id             << '\t'
	    << record.edge_kind           << '\t'
	    << record.from_instruction_id << '\t'
	    << record.to_instruction_id   << '\t'
	    << record.from_node_id        << '\t'
	    << record.to_node_id          << '\n';
}

void ManifestWriter::write(const CfgEdgeRecord &record) {
	out << "cfg_edge"            << '\t'
	    << record.edge_id        << '\t'
	    << record.from_bblock_id << '\t'
	    << record.to_bblock_id   << '\t'
	    << record.from_instr_id  << '\t'
	    << record.to_instr_id    << '\t'
	    << record.from_node_id   << '\t'
	    << record.to_node_id     << '\n';
}

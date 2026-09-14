#pragma once
#include <cstdint>
#include <llvm/ADT/StringRef.h>

inline constexpr llvm::StringLiteral FONTNAME = "fontname=\"monospace\"";
inline constexpr llvm::StringLiteral NODE_PREFIX = "node";

#define NODE_PREFIX "node"

inline constexpr llvm::StringLiteral CLR_INSTR = "color=\"#4865a3\"";
inline constexpr llvm::StringLiteral CLR_IMM   = "color=\"#3b7741\"";

inline constexpr llvm::StringLiteral FILL_INSTR = "fillcolor=\"#80a0e2\"";
inline constexpr llvm::StringLiteral FILL_IMM   = "fillcolor=\"#75b279\"";

inline constexpr llvm::StringLiteral CLR_DATA = "color=\"#000000\"";
inline constexpr llvm::StringLiteral CLR_SEQ  = "color=\"#d96a65\"";

inline constexpr llvm::StringLiteral CLR_FUNC   = "bgcolor=\"#f0f0f0\"";
inline constexpr llvm::StringLiteral CLR_BBLOCK = "bgcolor=\"#e0e0e0\"";

using NodeId   = uintptr_t;
using StableId = uint64_t;

static constexpr uint64_t NODE_TAG_SHIFT = 60;

enum class NodeTag : uint64_t {
	Instruction     = 1,
	Synthetic       = 2,
	FunctionCluster = 3,
	BBlockCluster   = 4
};

inline NodeId make_node_id(NodeTag tag, StableId id) {
	return (static_cast<uint64_t>(tag) << NODE_TAG_SHIFT) | id;
}

inline NodeId make_instr_node_id(StableId id) {
	return make_node_id(NodeTag::Instruction, id);
}

inline NodeId make_synthetic_node_id(StableId id) {
	return make_node_id(NodeTag::Synthetic, id);
}

inline NodeId make_function_cluster_id(StableId id) {
	return make_node_id(NodeTag::FunctionCluster, id);
}

inline NodeId make_bblock_cluster_id(StableId id) {
	return make_node_id(NodeTag::BBlockCluster, id);
}

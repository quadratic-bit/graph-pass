#!/usr/bin/env python3
import sys
import json
from pathlib import Path
from collections import Counter, defaultdict
from dataclasses import dataclass


FONTNAME = 'fontname="DepartureMono Nerd Font"'
NODE_PREFIX = "node"

CLR_INSTR = "#4865a3"
FILL_INSTR = "#80a0e2"

CLR_IMM = "#3b7741"
FILL_IMM = "#75b279"

CLR_DATA = "#000000"
CLR_SEQ = "#000000"

CLR_FUNC = "#f0f0f0"
CLR_BBLOCK = "#e0e0e0"

DIM_FUNC = "#e3e3e3"
DIM_BBLOCK = "#f1f1f1"

DIM_INSTR_CLR = "#8997b5"
DIM_INSTR_FILL = "#cfd7ea"

DIM_IMM_CLR = "#7f967f"
DIM_IMM_FILL = "#d3dfd3"

EDGE_UNVISITED = "#a9a9a9"
EDGE_COLD = "#5b8cff"
EDGE_HOT = "#d96a65"

FUNC_COLD = "#d7e7ff"
FUNC_HOT = "#ffd4d0"


@dataclass
class ModuleRow:
    module_id: int
    graph_name: str
    source_path: str


@dataclass
class FunctionRow:
    function_id: int
    cluster_node_id: int
    name: str


@dataclass
class BBlockRow:
    bblock_id: int
    function_id: int
    cluster_node_id: int
    entry_instruction_id: int
    name: str


@dataclass
class InstructionRow:
    instruction_id: int
    bblock_id: int
    node_id: int
    opcode_name: str
    rendered_label: str


@dataclass
class SyntheticRow:
    synthetic_id: int
    owner_instruction_id: int
    node_id: int
    rendered_label: str


@dataclass
class EdgeRow:
    edge_id: int
    edge_kind: str
    from_instruction_id: int
    to_instruction_id: int
    from_node_id: int
    to_node_id: int


@dataclass
class CFGEdgeRow:
    edge_id: int
    from_bblock_id: int
    to_bblock_id: int
    from_instruction_id: int
    to_instruction_id: int
    from_node_id: int
    to_node_id: int


def die(msg: str) -> None:
    print(msg, file=sys.stderr)
    sys.exit(1)


def unescape_manifest_field(text: str) -> str:
    out = []
    i = 0
    while i < len(text):
        if text[i] != "\\":
            out.append(text[i])
            i += 1
            continue

        i += 1
        if i >= len(text):
            out.append("\\")
            break

        c = text[i]
        if c == "t":
            out.append("\t")
        elif c == "n":
            out.append("\n")
        elif c == "r":
            out.append("\r")
        elif c == "\\":
            out.append("\\")
        else:
            out.append(c)
        i += 1
    return "".join(out)


def dot_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def lerp(a: int, b: int, t: float) -> int:
    return round(a + (b - a) * t)


def mix_color(c0: str, c1: str, t: float) -> str:
    t = max(0.0, min(1.0, t))
    r0, g0, b0 = int(c0[1:3], 16), int(c0[3:5], 16), int(c0[5:7], 16)
    r1, g1, b1 = int(c1[1:3], 16), int(c1[3:5], 16), int(c1[5:7], 16)
    r = lerp(r0, r1, t)
    g = lerp(g0, g1, t)
    b = lerp(b0, b1, t)
    return f"#{r:02x}{g:02x}{b:02x}"

def mix_color_power(c0: str, c1: str, t: float, gamma: float = 0.35) -> str:
    t = max(0.0, min(1.0, t))
    t = t ** gamma

    r0, g0, b0 = int(c0[1:3], 16), int(c0[3:5], 16), int(c0[5:7], 16)
    r1, g1, b1 = int(c1[1:3], 16), int(c1[3:5], 16), int(c1[5:7], 16)

    r = round(r0 + (r1 - r0) * t)
    g = round(g0 + (g1 - g0) * t)
    b = round(b0 + (b1 - b0) * t)

    return f"#{r:02x}{g:02x}{b:02x}"


def parse_manifest(path: str):
    module = None
    functions = {}
    bblocks = {}
    instructions = {}
    synthetics = {}
    edges = []
    cfg_edges = {}

    with open(path, "r", encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]

    if not lines:
        die("empty manifest")

    hdr = lines[0].split("\t")
    if len(hdr) != 2 or hdr[0] != "graphpass-manifest":
        die("invalid manifest header")
    if hdr[1] != "1":
        die(f"unsupported manifest version: {hdr[1]}")

    for line in lines[1:]:
        if not line:
            continue

        cols = line.split("\t")
        tag = cols[0]

        if tag == "module":
            if len(cols) != 4:
                die(f"bad module row: {line}")
            module = ModuleRow(
                module_id=int(cols[1]),
                graph_name=unescape_manifest_field(cols[2]),
                source_path=unescape_manifest_field(cols[3]),
            )

        elif tag == "function":
            if len(cols) != 4:
                die(f"bad function row: {line}")
            row = FunctionRow(
                function_id=int(cols[1]),
                cluster_node_id=int(cols[2]),
                name=unescape_manifest_field(cols[3]),
            )
            functions[row.function_id] = row

        elif tag == "bblock":
            if len(cols) != 6:
                die(f"bad bblock row: {line}")
            row = BBlockRow(
                bblock_id=int(cols[1]),
                function_id=int(cols[2]),
                cluster_node_id=int(cols[3]),
                entry_instruction_id=int(cols[4]),
                name=unescape_manifest_field(cols[5]),
            )
            bblocks[row.bblock_id] = row

        elif tag == "instruction":
            if len(cols) != 6:
                die(f"bad instruction row: {line}")
            row = InstructionRow(
                instruction_id=int(cols[1]),
                bblock_id=int(cols[2]),
                node_id=int(cols[3]),
                opcode_name=unescape_manifest_field(cols[4]),
                rendered_label=unescape_manifest_field(cols[5]),
            )
            instructions[row.instruction_id] = row

        elif tag == "synthetic":
            if len(cols) != 5:
                die(f"bad synthetic row: {line}")
            row = SyntheticRow(
                synthetic_id=int(cols[1]),
                owner_instruction_id=int(cols[2]),
                node_id=int(cols[3]),
                rendered_label=unescape_manifest_field(cols[4]),
            )
            synthetics[row.synthetic_id] = row

        elif tag == "edge":
            if len(cols) != 7:
                die(f"bad edge row: {line}")
            edges.append(
                EdgeRow(
                    edge_id=int(cols[1]),
                    edge_kind=cols[2],
                    from_instruction_id=int(cols[3]),
                    to_instruction_id=int(cols[4]),
                    from_node_id=int(cols[5]),
                    to_node_id=int(cols[6]),
                )
            )

        elif tag == "cfg_edge":
            if len(cols) != 8:
                die(f"bad cfg_edge row: {line}")
            row = CFGEdgeRow(
                edge_id=int(cols[1]),
                from_bblock_id=int(cols[2]),
                to_bblock_id=int(cols[3]),
                from_instruction_id=int(cols[4]),
                to_instruction_id=int(cols[5]),
                from_node_id=int(cols[6]),
                to_node_id=int(cols[7]),
            )
            cfg_edges[row.edge_id] = row

        else:
            die(f"unknown manifest tag: {tag}")

    if module is None:
        die("manifest missing module row")

    return module, functions, bblocks, instructions, synthetics, edges, cfg_edges


def parse_glog(path: str):
    with open(path, "r", encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f if line.strip()]

    if not lines:
        die("empty glog")

    hdr = lines[0].split("\t")
    if len(hdr) != 3 or hdr[0] != "GLOG" or hdr[1] != "1":
        die("invalid glog header")

    module_id = int(hdr[2])
    bb_hits = Counter()
    edge_hits = Counter()
    call_hits = Counter()
    events = []

    for line in lines[1:]:
        cols = line.split("\t")
        if len(cols) != 4:
            die(f"bad glog row: {line}")

        seq = int(cols[0])
        tid = int(cols[1])
        tag = cols[2]
        ident = int(cols[3])

        events.append((seq, tid, tag, ident))

        if tag == "BB":
            bb_hits[ident] += 1
        elif tag == "EDGE":
            edge_hits[ident] += 1
        elif tag == "CALL":
            call_hits[ident] += 1
        else:
            die(f"unknown glog tag: {tag}")

    return module_id, bb_hits, edge_hits, call_hits, events


def label_with_count(name: str, count: int) -> str:
    if name:
        return f"{name} [visits={count}]"
    return f"[visits={count}]"


def edge_penwidth(edge_count: int, function_total_edge_tally: int) -> float:
    if edge_count <= 0 or function_total_edge_tally <= 0:
        return 2.0
    share = edge_count / function_total_edge_tally
    return 2.0 + 8.0 * share


def edge_color(edge_count: int, module_total_edge_tally: int) -> str:
    if edge_count <= 0:
        return EDGE_UNVISITED
    if module_total_edge_tally <= 0:
        return EDGE_COLD
    return mix_color_power(EDGE_COLD, EDGE_HOT, edge_count / module_total_edge_tally, 0.35)


def function_bg(function_count: int, module_total_function_tally: int) -> str:
    if function_count <= 0:
        return DIM_FUNC
    if module_total_function_tally <= 0:
        return CLR_FUNC
    return mix_color(FUNC_COLD, FUNC_HOT, function_count / module_total_function_tally)


def bblock_bg(bblock_count: int) -> str:
    return CLR_BBLOCK if bblock_count > 0 else DIM_BBLOCK


def instr_colors(visited: bool):
    if visited:
        return CLR_INSTR, FILL_INSTR
    return DIM_INSTR_CLR, DIM_INSTR_FILL


def synthetic_colors(visited: bool):
    if visited:
        return CLR_IMM, FILL_IMM
    return DIM_IMM_CLR, DIM_IMM_FILL


def resolve_manifest_and_glog(argv):
    if len(argv) == 3:
        return Path(argv[1]), Path(argv[2])

    if len(argv) == 2:
        bundle = Path(argv[1])

        if bundle.is_dir():
            run_json_path = bundle / "run.json"
        else:
            run_json_path = bundle

        with open(run_json_path, "r", encoding="utf-8") as f:
            run_meta = json.load(f)

        return Path(run_meta["manifest"]), Path(run_meta["glog"])

    die("usage: enrich_graph.py <manifest.tsv> <graphpass.glog>\n"
        "   or: enrich_graph.py <bundle-dir>\n"
        "   or: enrich_graph.py <run.json>")


def main():
    manifest_path, glog_path = resolve_manifest_and_glog(sys.argv)

    (
        module,
        functions,
        bblocks,
        instructions,
        synthetics,
        edges,
        cfg_edges,
    ) = parse_manifest(str(manifest_path))

    glog_module_id, bb_hits, edge_hits, call_hits, events = parse_glog(str(glog_path))

    if glog_module_id != module.module_id:
        die(
            f"module id mismatch: manifest={module.module_id} glog={glog_module_id}"
        )

    # validation of runtime ids
    for bb_id in bb_hits:
        if bb_id not in bblocks:
            die(f"glog references unknown bblock id: {bb_id}")

    for edge_id in edge_hits:
        if edge_id not in cfg_edges:
            die(f"glog references unknown cfg edge id: {edge_id}")

    for inst_id in call_hits:
        if inst_id not in instructions:
            die(f"glog references unknown instruction id: {inst_id}")
        if instructions[inst_id].opcode_name != "call":
            die(f"CALL references non-call instruction id: {inst_id}")

    blocks_by_function = defaultdict(list)
    for row in sorted(bblocks.values(), key=lambda x: x.bblock_id):
        blocks_by_function[row.function_id].append(row)

    insts_by_block = defaultdict(list)
    for row in sorted(instructions.values(), key=lambda x: x.instruction_id):
        insts_by_block[row.bblock_id].append(row)

    synthetics_by_owner = defaultdict(list)
    for row in sorted(synthetics.values(), key=lambda x: x.synthetic_id):
        synthetics_by_owner[row.owner_instruction_id].append(row)

    # display edges grouped by function
    instruction_to_function = {}
    for inst in instructions.values():
        bb = bblocks[inst.bblock_id]
        instruction_to_function[inst.instruction_id] = bb.function_id

    edges_by_function = defaultdict(list)
    for row in sorted(edges, key=lambda x: x.edge_id):
        fn_id = instruction_to_function[row.from_instruction_id]
        edges_by_function[fn_id].append(row)

    cfg_by_instr_pair = {}
    for row in cfg_edges.values():
        cfg_by_instr_pair[(row.from_instruction_id, row.to_instruction_id)] = row

    function_visit_counts = {}
    for function_id, fn_blocks in blocks_by_function.items():
        entry_block = min(fn_blocks, key=lambda b: b.bblock_id)
        function_visit_counts[function_id] = bb_hits[entry_block.bblock_id]

    function_total_tally = sum(function_visit_counts.values())
    module_total_edge_tally = sum(edge_hits.values())

    function_edge_totals = {}
    for function_id, fn_blocks in blocks_by_function.items():
        fn_block_ids = {b.bblock_id for b in fn_blocks}
        total = 0
        for cfg in cfg_edges.values():
            if cfg.from_bblock_id in fn_block_ids:
                total += edge_hits[cfg.edge_id]
        function_edge_totals[function_id] = total

    print(f'digraph {dot_escape(module.graph_name)} {{')
    print('\trankdir=TB;')
    print('\tdpi=300')
    print('\tnode [shape=box];')
    print(f'\tlabel="{dot_escape(module.source_path)}"')
    print(f'\t{FONTNAME}')

    for function_id in sorted(functions):
        fn = functions[function_id]
        fn_count = function_visit_counts.get(function_id, 0)
        fn_bg = function_bg(fn_count, function_total_tally)
        fn_label = label_with_count(fn.name, fn_count)

        print()
        print(f'\tsubgraph cluster_{fn.cluster_node_id} {{')
        print(f'\t\tlabel="{dot_escape(fn_label)}"')
        print(f'\t\t{FONTNAME}')
        print(f'\t\tbgcolor="{fn_bg}"')

        # emit nodes in the same structural order: by block, by instruction
        for bb in blocks_by_function[function_id]:
            bb_count = bb_hits[bb.bblock_id]
            visited = bb_count > 0

            for inst in insts_by_block[bb.bblock_id]:
                for syn in synthetics_by_owner.get(inst.instruction_id, []):
                    syn_clr, syn_fill = synthetic_colors(visited)
                    print(
                        f'\t\t{NODE_PREFIX}{syn.node_id} '
                        f'[label="{dot_escape(syn.rendered_label)}",'
                        f'style=filled,'
                        f'color="{syn_clr}",'
                        f'fillcolor="{syn_fill}",'
                        f'{FONTNAME}];'
                    )
                    print(
                        f'\t\t{NODE_PREFIX}{syn.node_id} -> {NODE_PREFIX}{inst.node_id} '
                        f'[label="",style=dashed,color="{CLR_DATA}",{FONTNAME}]'
                    )

                instr_clr, instr_fill = instr_colors(visited)
                print(
                    f'\t\t{NODE_PREFIX}{inst.node_id} '
                    f'[label="{dot_escape(inst.rendered_label)}",'
                    f'style=filled,'
                    f'color="{instr_clr}",'
                    f'fillcolor="{instr_fill}",'
                    f'{FONTNAME}];'
                )

        # emit static display edges, enriching only cross-bblock block edges
        for edge in edges_by_function[function_id]:
            if edge.edge_kind == "data":
                print(
                    f'\t\t{NODE_PREFIX}{edge.from_node_id} -> {NODE_PREFIX}{edge.to_node_id} '
                    f'[label="",style=dashed,color="{CLR_DATA}",{FONTNAME}]'
                )
                continue

            if edge.edge_kind == "seq":
                print(
                    f'\t\t{NODE_PREFIX}{edge.from_node_id} -> {NODE_PREFIX}{edge.to_node_id} '
                    f'[color="{CLR_SEQ}"]'
                )
                continue

            if edge.edge_kind != "block":
                die(f"unknown display edge kind: {edge.edge_kind}")

            cfg = cfg_by_instr_pair.get((edge.from_instruction_id, edge.to_instruction_id))
            if cfg is None:
                die(
                    f"missing cfg_edge for block edge "
                    f"{edge.from_instruction_id}->{edge.to_instruction_id}"
                )

            count = edge_hits[cfg.edge_id]
            penwidth = edge_penwidth(count, function_edge_totals.get(function_id, 0))
            color = edge_color(count, module_total_edge_tally)

            print(
                f'\t\t{NODE_PREFIX}{edge.from_node_id} -> {NODE_PREFIX}{edge.to_node_id} '
                f'[style=solid,penwidth={penwidth:.2f},color="{color}",{FONTNAME}]'
            )

        # emit bblock clusters with count labels
        for bb in blocks_by_function[function_id]:
            bb_count = bb_hits[bb.bblock_id]
            bb_label = label_with_count(bb.name, bb_count)
            bb_bg = bblock_bg(bb_count)

            print(f'\t\tsubgraph cluster_{bb.cluster_node_id} {{')
            print(f'\t\t\tlabel="{dot_escape(bb_label)}"')
            print(f'\t\t\tbgcolor="{bb_bg}"')
            for inst in insts_by_block[bb.bblock_id]:
                print(f'\t\t\t{NODE_PREFIX}{inst.node_id}')
            print('\t\t}')

        print('\t}')

    print('}')


if __name__ == "__main__":
    main()

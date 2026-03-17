#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <string>
#include <system_error>

#include "graphpass/ids.hpp"
#include "graphpass/instrumentation.hpp"
#include "graphpass/render.hpp"

using namespace llvm;

struct GraphPass : public PassInfoMixin<GraphPass> {
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
		auto source_path = M.getSourceFileName();
		auto filename = source_path.substr(source_path.find_last_of('/') + 1);
		std::replace(filename.begin(), filename.end(), '.', '_');

		ModuleSlotTracker slot_tracker(&M);

		StableIds stable_ids = collect_stable_ids(M);
		RuntimeIds runtime_ids = collect_runtime_ids(M);

		std::string manifest_path = filename + ".manifest.tsv";
		std::error_code manifest_error;
		raw_fd_ostream manifest(manifest_path, manifest_error, sys::fs::OF_Text);

		if (manifest_error) {
			errs() << "GraphPass: failed to open manifest file '"
			       << manifest_path << "': "
			       << manifest_error.message() << '\n';
			return PreservedAnalyses::all();
		}

		emit_graph_and_manifest(filename, source_path, M, slot_tracker, llvm::outs(), manifest, stable_ids, runtime_ids);

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

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/FileSystem.h>

#include <system_error>

#include "config.hpp"
#include "ids.hpp"
#include "instrumentation.hpp"
#include "render.hpp"

using namespace llvm;

struct GraphPass : public PassInfoMixin<GraphPass> {
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
		GraphPassConfig config = resolve_graphpass_config(M);

		ModuleSlotTracker slot_tracker(&M);

		StableIds stable_ids = collect_stable_ids(M);
		RuntimeIds runtime_ids = collect_runtime_ids(M);

		std::error_code manifest_error;
		raw_fd_ostream manifest(config.manifest_path, manifest_error, sys::fs::OF_Text);

		if (manifest_error) {
			errs() << "GraphPass: failed to open manifest file '"
			       << config.manifest_path << "': "
			       << manifest_error.message() << '\n';
			return PreservedAnalyses::all();
		}

		emit_graph_and_manifest(
			config.artifact_name,
			config.source_path,
			M,
			slot_tracker,
			llvm::outs(),
			manifest,
			stable_ids,
			runtime_ids
		);

		manifest.close();
		if (manifest.has_error()) {
			errs() << "GraphPass: failed while writing manifest file '"
			       << config.manifest_path << "'\n";
			manifest.clear_error();
		}

		instrument_runtime_logging(M, stable_ids, runtime_ids);
		return PreservedAnalyses::none();
	}
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

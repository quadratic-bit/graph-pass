#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace llvm;

static std::string getenv_or_empty(const char *name) {
	const char *value = std::getenv(name);
	return value ? std::string(value) : std::string();
}

static std::string sanitize_artifact_name(std::string name) {
	if (name.empty()) return "graphpass";

	for (char &c : name) {
		unsigned char uc = static_cast<unsigned char>(c);
		if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9') || c == '_' || c == '-')
			continue;
		c = '_';
	}

	unsigned char first = static_cast<unsigned char>(name.front());
	if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || name.front() == '_'))
		name = "run_" + name;

	return name;
}

static std::string derive_artifact_name_from_source(const std::string &source_path) {
	namespace fs = std::filesystem;

	std::string stem = fs::path(source_path).stem().string();
	if (stem.empty()) stem = "graphpass";
	return sanitize_artifact_name(stem);
}

GraphPassConfig resolve_graphpass_config(const Module &M) {
	GraphPassConfig config;

	config.source_path = M.getSourceFileName();
	if (config.source_path.empty())
		config.source_path = M.getModuleIdentifier();
	if (config.source_path.empty())
		config.source_path = "module";

	config.artifact_name = getenv_or_empty("GRAPH_PASS_NAME");
	if (config.artifact_name.empty())
		config.artifact_name = derive_artifact_name_from_source(config.source_path);
	else
		config.artifact_name = sanitize_artifact_name(config.artifact_name);

	config.manifest_path = getenv_or_empty("GRAPH_PASS_MANIFEST");
	if (config.manifest_path.empty())
		config.manifest_path = config.artifact_name + ".manifest.tsv";

	return config;
}

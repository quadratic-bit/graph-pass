LLVM_CONFIG ?= llvm-config
CLANG       ?= clang
CLANGXX     ?= clang++
PYTHON      ?= python3

EXAMPLE ?= fact
SRC     ?= examples/$(EXAMPLE).c
OPT     ?= -O1
ARGS    ?=

BUILD_DIR := out/build
OUT_DIR   := out/$(EXAMPLE)

PASS_SO := $(BUILD_DIR)/graphPass.so
RT_OBJ  := $(BUILD_DIR)/graphpass_rt.o

BIN      := $(OUT_DIR)/$(EXAMPLE).out
DOT      := $(OUT_DIR)/$(EXAMPLE).dot
GLOG     := $(OUT_DIR)/$(EXAMPLE).glog
MANIFEST := $(OUT_DIR)/$(EXAMPLE)_c.manifest.tsv
RUNTIME_DOT := $(OUT_DIR)/$(EXAMPLE).runtime.dot

.PHONY: build graph run enrich rerun clean

build: $(PASS_SO) $(RT_OBJ)

graph: build
	mkdir -p $(OUT_DIR)
	$(CLANG) -fpass-plugin=./$(PASS_SO) $(SRC) $(RT_OBJ) $(OPT) -o $(BIN) > $(DOT)
	@if [ -f $(EXAMPLE)_c.manifest.tsv ]; then mv -f $(EXAMPLE)_c.manifest.tsv $(MANIFEST); fi

run: graph
	GRAPH_PASS_LOG=$(GLOG) ./$(BIN) $(ARGS)

enrich:
	mkdir -p $(OUT_DIR)
	$(PYTHON) enrich_graph.py $(MANIFEST) $(GLOG) > $(RUNTIME_DOT)

rerun: run enrich

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PASS_SO): graph_pass.cpp | $(BUILD_DIR)
	$(CLANGXX) graph_pass.cpp -fPIC -shared -I$$($(LLVM_CONFIG) --includedir) -o $(PASS_SO)

$(RT_OBJ): graphpass_rt.c | $(BUILD_DIR)
	$(CLANG) -c graphpass_rt.c -O2 -o $(RT_OBJ)

clean:
	rm -rf out
	rm -f *_c.manifest.tsv

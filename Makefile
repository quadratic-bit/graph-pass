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

GRAPH_PASS_SRCS := \
	src/pass.cpp \
	src/ids.cpp \
	src/manifest.cpp \
	src/instrumentation.cpp \
	src/config.cpp \
	src/render.cpp

BIN         := $(OUT_DIR)/$(EXAMPLE).out
DOT         := $(OUT_DIR)/$(EXAMPLE).dot
GLOG        := $(OUT_DIR)/$(EXAMPLE).glog
MANIFEST    := $(EXAMPLE)_c.manifest.tsv
RUNTIME_DOT := $(OUT_DIR)/$(EXAMPLE).runtime.dot

.PHONY: build graph run enrich rerun clean

build: $(PASS_SO) $(RT_OBJ)

graph: build
	mkdir -p $(OUT_DIR)
	$(CLANG) -fpass-plugin=./$(PASS_SO) $(SRC) $(RT_OBJ) $(OPT) -o $(BIN) > $(DOT)

run: graph
	GRAPH_PASS_LOG=$(GLOG) ./$(BIN) $(ARGS)

enrich:
	mkdir -p $(OUT_DIR)
	$(PYTHON) enrich_graph.py $(MANIFEST) $(GLOG) > $(RUNTIME_DOT)

rerun: run enrich

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PASS_SO): $(GRAPH_PASS_SRCS) src/common.hpp src/config.hpp src/ids.hpp src/manifest.hpp src/instrumentation.hpp src/render.hpp | $(BUILD_DIR)
	$(CLANGXX) -fPIC -shared -I. -I$$($(LLVM_CONFIG) --includedir) $(GRAPH_PASS_SRCS) -o $(PASS_SO)

$(RT_OBJ): graphpass_rt.c | $(BUILD_DIR)
	$(CLANG) -c graphpass_rt.c -O2 -o $(RT_OBJ)

clean:
	rm -rf out
	rm -f *_c.manifest.tsv

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

INCLUDE_DIR := include
RUNTIME_SRC := runtime/graphpass_rt.c
ENRICH_TOOL := tools/enrich_graph.py

PASS_SO := $(BUILD_DIR)/graphPass.so
RT_OBJ  := $(BUILD_DIR)/graphpass_rt.o

GRAPH_PASS_SRCS := \
	src/pass.cpp \
	src/ids.cpp \
	src/manifest.cpp \
	src/instrumentation.cpp \
	src/config.cpp \
	src/render.cpp

GRAPH_PASS_HEADERS := \
	include/graphpass/common.hpp \
	include/graphpass/config.hpp \
	include/graphpass/ids.hpp \
	include/graphpass/manifest.hpp \
	include/graphpass/instrumentation.hpp \
	include/graphpass/render.hpp

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
	$(PYTHON) $(ENRICH_TOOL) $(MANIFEST) $(GLOG) > $(RUNTIME_DOT)

rerun: run enrich

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PASS_SO): $(GRAPH_PASS_SRCS) $(GRAPH_PASS_HEADERS) | $(BUILD_DIR)
	$(CLANGXX) -fPIC -shared \
		-I$(INCLUDE_DIR) \
		-I$$($(LLVM_CONFIG) --includedir) \
		$(GRAPH_PASS_SRCS) \
		-o $(PASS_SO)

$(RT_OBJ): $(RUNTIME_SRC) | $(BUILD_DIR)
	$(CLANG) -c $(RUNTIME_SRC) -O2 -o $(RT_OBJ)

clean:
	rm -rf out

# Open Controls - Root Makefile
# Copyright (C) 2026 Daher Alfawares

CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -O2

BIN_DIR := bin
TOOLS_DIR := tools
MODELS_DIR := models

# Tool definitions
TOOLS := mdl_to_oc mdl_to_yaml mdl_to_cpp mdl_dump mdl_lint oc_to_mdl

# Find all MDL files in models directory
MDL_FILES := $(wildcard $(MODELS_DIR)/*.mdl)

.PHONY: all clean install uninstall test $(TOOLS) help

all: $(BIN_DIR) $(TOOLS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

mdl_to_oc: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/mdl_to_oc/main.cpp

mdl_to_yaml: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/mdl_to_yaml/main.cpp

mdl_to_cpp: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/mdl_to_cpp/main.cpp

mdl_dump: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/mdl_dump/main.cpp

mdl_lint: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/mdl_lint/main.cpp

oc_to_mdl: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/$@ $(TOOLS_DIR)/oc_to_mdl/main.cpp

test: mdl_lint
	@echo ""
	@echo "Running MDL lint on all models in $(MODELS_DIR)/"
	@echo ""
	@if [ -z "$(MDL_FILES)" ]; then \
		echo "No .mdl files found in $(MODELS_DIR)/"; \
		exit 1; \
	fi
	@$(BIN_DIR)/mdl_lint $(MDL_FILES)

clean:
	rm -rf $(BIN_DIR)
	rm -f $(TOOLS_DIR)/mdl_to_oc/mdl_to_oc
	rm -f $(TOOLS_DIR)/mdl_to_yaml/mdl_to_yaml
	rm -f $(TOOLS_DIR)/mdl_to_cpp/mdl_to_cpp
	rm -f $(TOOLS_DIR)/mdl_dump/mdl_dump
	rm -f $(TOOLS_DIR)/mdl_lint/mdl_lint
	rm -f $(TOOLS_DIR)/oc_to_mdl/oc_to_mdl

install: all
	install -d /usr/local/bin
	install -m 755 $(BIN_DIR)/mdl_to_oc /usr/local/bin/
	install -m 755 $(BIN_DIR)/mdl_to_yaml /usr/local/bin/
	install -m 755 $(BIN_DIR)/mdl_to_cpp /usr/local/bin/
	install -m 755 $(BIN_DIR)/mdl_lint /usr/local/bin/
	install -m 755 $(BIN_DIR)/oc_to_mdl /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/mdl_to_oc
	rm -f /usr/local/bin/mdl_to_yaml
	rm -f /usr/local/bin/mdl_to_cpp
	rm -f /usr/local/bin/mdl_lint
	rm -f /usr/local/bin/oc_to_mdl

help:
	@echo "Open Controls Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all tools (default)"
	@echo "  test      - Run mdl_lint on all models in models/"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install tools to /usr/local/bin"
	@echo "  uninstall - Remove installed tools"
	@echo ""
	@echo "Individual tools:"
	@echo "  mdl_to_oc   - MDL to OC format converter"
	@echo "  mdl_to_yaml - MDL to YAML schema converter"
	@echo "  mdl_to_cpp  - MDL to C++ code generator"
	@echo "  mdl_dump    - MDL structure inspector"
	@echo "  mdl_lint    - MDL model validator"
	@echo "  oc_to_mdl   - OC to MDL format converter"

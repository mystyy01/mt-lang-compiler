CXX := g++
MTC_VERSION ?= $(shell tr -d '\n' < VERSION 2>/dev/null || echo dev)
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -Isrc -DMTC_VERSION=\"$(MTC_VERSION)\"
DEPFLAGS := -MMD -MP
LDFLAGS :=

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR := build
BIN_DIR := dist
TARGET := $(BIN_DIR)/mtc
LSP_TARGET := $(BIN_DIR)/mtc_lsp
FUZZ_PARSER_TARGET := $(BIN_DIR)/mtc_fuzz_parser
TEST_TARGET := $(BIN_DIR)/tokenizer_test
PARSER_TEST_TARGET := $(BIN_DIR)/parser_test
SEMANTIC_TEST_TARGET := $(BIN_DIR)/semantic_test
CODEGEN_TEST_TARGET := $(BIN_DIR)/codegen_test
E2E_TEST_SCRIPT := $(TEST_DIR)/run_e2e_tests.sh
PERF_TEST_SCRIPT := $(TEST_DIR)/run_perf_gate.sh
LSP_SMOKE_SCRIPT := $(TEST_DIR)/run_lsp_smoke.sh
COMPILER_INTEGRATION_TEST_SCRIPT := $(TEST_DIR)/run_compiler_integration_tests.sh
PARITY_TEST_SCRIPT := $(TEST_DIR)/run_parity_matrix.sh
DIAGNOSTICS_TEST_SCRIPT := $(TEST_DIR)/run_diagnostics_tests.sh
FUZZ_TEST_SCRIPT := $(TEST_DIR)/run_fuzz_smoke.sh
COMPILE_BENCH_SCRIPT := $(TEST_DIR)/run_compile_benchmarks.sh
OPT_LEVEL_VALIDATION_SCRIPT := $(TEST_DIR)/run_opt_level_validation.sh
RUNTIME_ABI_TEST_SCRIPT := $(TEST_DIR)/run_runtime_abi_contract.sh
DETERMINISM_TEST_SCRIPT := $(TEST_DIR)/run_codegen_determinism.sh
RUNTIME_FAILURE_TEST_SCRIPT := $(TEST_DIR)/run_runtime_failure_tests.sh
VERSIONING_TEST_SCRIPT := $(TEST_DIR)/run_versioning_tests.sh
ENTRYPOINT_SRC := $(firstword $(wildcard $(SRC_DIR)/compiler.cpp) $(wildcard $(SRC_DIR)/main.cpp) $(wildcard ./main.cpp))

SRC := $(shell find $(SRC_DIR) -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) 2>/dev/null)

ifeq ($(strip $(SRC)),)
SRC := $(shell find . -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) 2>/dev/null)
endif

OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.cpp,$(SRC))) \
       $(patsubst $(SRC_DIR)/%.cc,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.cc,$(SRC))) \
       $(patsubst $(SRC_DIR)/%.cxx,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.cxx,$(SRC))) \
       $(patsubst ./%.cpp,$(BUILD_DIR)/%.o,$(filter ./%.cpp,$(SRC))) \
       $(patsubst ./%.cc,$(BUILD_DIR)/%.o,$(filter ./%.cc,$(SRC))) \
       $(patsubst ./%.cxx,$(BUILD_DIR)/%.o,$(filter ./%.cxx,$(SRC)))
DEPS := $(OBJ:.o=.d) $(BUILD_DIR)/lsp_server.d $(BUILD_DIR)/fuzz_parser.d

.PHONY: all debug release run test test-tokenizer test-parser test-semantic test-codegen test-e2e test-compiler test-parity test-diagnostics test-fuzz test-lsp test-abi test-determinism test-runtime-failures test-versioning perf bench-compile test-opt-level check lsp clean print-src

-include $(DEPS)

all: release

release: CXXFLAGS += -O2 -DNDEBUG
ifeq ($(strip $(ENTRYPOINT_SRC)),)
release: $(OBJ) $(LSP_TARGET)
else
release: $(TARGET) $(LSP_TARGET)
endif

debug: CXXFLAGS += -O0 -g -DDEBUG

ifeq ($(strip $(ENTRYPOINT_SRC)),)
debug: $(OBJ) $(LSP_TARGET)
else
debug: $(TARGET) $(LSP_TARGET)
endif

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/lsp_server.o: tools/lsp_server.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(LSP_TARGET): $(BUILD_DIR)/lsp_server.o $(BUILD_DIR)/tokenizer.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/semantic.o $(BUILD_DIR)/libc_functions.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/fuzz_parser.o: tools/fuzz_parser.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(FUZZ_PARSER_TARGET): $(BUILD_DIR)/fuzz_parser.o $(BUILD_DIR)/tokenizer.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/semantic.o $(BUILD_DIR)/libc_functions.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

ifeq ($(strip $(ENTRYPOINT_SRC)),)
run:
	@echo "No entry point found. Add src/main.cpp (or ./main.cpp) to run the binary."
	@exit 1
else
run: $(TARGET)
	./$(TARGET)
endif

$(TEST_TARGET): $(TEST_DIR)/tokenizer_test.cpp $(BUILD_DIR)/tokenizer.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(PARSER_TEST_TARGET): $(TEST_DIR)/parser_test.cpp $(BUILD_DIR)/parser.o $(BUILD_DIR)/tokenizer.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(SEMANTIC_TEST_TARGET): $(TEST_DIR)/semantic_test.cpp $(BUILD_DIR)/semantic.o $(BUILD_DIR)/libc_functions.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/tokenizer.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(CODEGEN_TEST_TARGET): $(TEST_DIR)/codegen_test.cpp $(BUILD_DIR)/codegen.o $(BUILD_DIR)/libc_functions.o $(BUILD_DIR)/parser.o $(BUILD_DIR)/tokenizer.o
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test-tokenizer: $(TEST_TARGET)
	./$(TEST_TARGET)

test-parser: $(PARSER_TEST_TARGET)
	./$(PARSER_TEST_TARGET)

test-semantic: $(SEMANTIC_TEST_TARGET)
	./$(SEMANTIC_TEST_TARGET)

test-codegen: $(CODEGEN_TEST_TARGET)
	./$(CODEGEN_TEST_TARGET)

test: test-tokenizer test-parser test-semantic test-codegen test-e2e test-compiler test-parity test-diagnostics test-abi test-determinism test-runtime-failures test-versioning

test-e2e: $(TARGET)
	bash $(E2E_TEST_SCRIPT)

test-compiler: $(TARGET)
	bash $(COMPILER_INTEGRATION_TEST_SCRIPT)

test-parity: $(TARGET)
	bash $(PARITY_TEST_SCRIPT)

test-diagnostics: $(TARGET)
	bash $(DIAGNOSTICS_TEST_SCRIPT)

test-fuzz: $(FUZZ_PARSER_TARGET)
	bash $(FUZZ_TEST_SCRIPT)

test-lsp: $(LSP_TARGET)
	bash $(LSP_SMOKE_SCRIPT)

test-abi: $(TARGET)
	bash $(RUNTIME_ABI_TEST_SCRIPT)

test-determinism: $(TARGET)
	bash $(DETERMINISM_TEST_SCRIPT)

test-runtime-failures: $(TARGET)
	bash $(RUNTIME_FAILURE_TEST_SCRIPT)

test-versioning: $(TARGET)
	bash $(VERSIONING_TEST_SCRIPT)

perf: $(TARGET)
	bash $(PERF_TEST_SCRIPT)

bench-compile: $(TARGET)
	bash $(COMPILE_BENCH_SCRIPT)

test-opt-level: $(TARGET)
	bash $(OPT_LEVEL_VALIDATION_SCRIPT)

check: test test-lsp perf bench-compile test-opt-level test-fuzz

lsp: $(LSP_TARGET)

clean:
	rm -rf $(BUILD_DIR)
	if [ -d $(BIN_DIR) ]; then find $(BIN_DIR) -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true; fi

print-src:
	@printf '%s\n' $(SRC)

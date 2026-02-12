CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -Isrc
LDFLAGS :=

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR := build
BIN_DIR := dist
TARGET := $(BIN_DIR)/mtc
TEST_TARGET := $(BIN_DIR)/tokenizer_test
PARSER_TEST_TARGET := $(BIN_DIR)/parser_test
SEMANTIC_TEST_TARGET := $(BIN_DIR)/semantic_test
CODEGEN_TEST_TARGET := $(BIN_DIR)/codegen_test
E2E_TEST_SCRIPT := $(TEST_DIR)/run_e2e_tests.sh
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

.PHONY: all debug release run test test-tokenizer test-parser test-semantic test-codegen test-e2e clean print-src

all: release

release: CXXFLAGS += -O2 -DNDEBUG
ifeq ($(strip $(ENTRYPOINT_SRC)),)
release: $(OBJ)
else
release: $(TARGET)
endif

debug: CXXFLAGS += -O0 -g -DDEBUG

ifeq ($(strip $(ENTRYPOINT_SRC)),)
debug: $(OBJ)
else
debug: $(TARGET)
endif

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: ./%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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

test: test-tokenizer test-parser test-semantic test-codegen test-e2e

test-e2e: $(TARGET)
	bash $(E2E_TEST_SCRIPT)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

print-src:
	@printf '%s\n' $(SRC)

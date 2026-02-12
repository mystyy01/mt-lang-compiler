CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS :=

SRC_DIR := src
BUILD_DIR := build
BIN_DIR := dist
TARGET := $(BIN_DIR)/mtc

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

.PHONY: all debug release run clean print-src

all: release

release: CXXFLAGS += -O2 -DNDEBUG
release: $(TARGET)

debug: CXXFLAGS += -O0 -g -DDEBUG

debug: $(TARGET)

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

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

print-src:
	@printf '%s\n' $(SRC)

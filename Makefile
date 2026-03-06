MTC ?= mtc
LD ?= ld
BUILD_DIR ?= build
BIN_DIR ?= dist
TARGET ?= $(BIN_DIR)/mtc-self-host

ifneq ($(wildcard source),)
SOURCE_DIR ?= source
else ifneq ($(wildcard src),)
SOURCE_DIR ?= src
else
SOURCE_DIR ?= source
endif

MTC_SOURCES := $(shell find $(SOURCE_DIR) -type f -name '*.mtc' 2>/dev/null)
MAIN_SRC ?= $(SOURCE_DIR)/compiler.mtc
MAIN_OBJ := $(patsubst $(SOURCE_DIR)/%.mtc,$(BUILD_DIR)/%.o,$(MAIN_SRC))
OTHER_SOURCES := $(filter-out $(MAIN_SRC),$(MTC_SOURCES))
OTHER_OBJECTS := $(patsubst $(SOURCE_DIR)/%.mtc,$(BUILD_DIR)/%.o,$(OTHER_SOURCES))
OBJECTS := $(MAIN_OBJ) $(OTHER_OBJECTS)

.PHONY: all compile build run clean list check-mtc check-sources check-main

all: build

compile: check-mtc check-sources check-main $(OBJECTS)
	@echo "Built $(words $(OBJECTS)) object file(s) into $(BUILD_DIR)/"

build: compile
	@mkdir -p $(dir $(TARGET))
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJECTS)
	@echo "Linked executable: $(TARGET)"

run: build
	./$(TARGET)

check-mtc:
	@command -v $(MTC) >/dev/null 2>&1 || { \
		echo "Error: '$(MTC)' not found in PATH"; \
		exit 1; \
	}

check-sources:
	@test -n "$(MTC_SOURCES)" || { \
		echo "Error: no .mtc sources found under '$(SOURCE_DIR)'"; \
		exit 1; \
	}

check-main:
	@test -f "$(MAIN_SRC)" || { \
		echo "Error: main entry source '$(MAIN_SRC)' was not found"; \
		exit 1; \
	}

$(MAIN_OBJ): $(MAIN_SRC)
	@mkdir -p $(dir $@)
	$(MTC) -o $< $@

$(OTHER_OBJECTS): $(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.mtc
	@mkdir -p $(dir $@)
	$(MTC) --no-runtime -o $< $@

list: check-sources
	@printf '%s\n' $(MTC_SOURCES)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# make <filename>
# Compiles src/<filename>.mtc to build/<filename> and runs it.
.DEFAULT:
	@command -v $(MTC) >/dev/null 2>&1 || { \
		echo "Error: '$(MTC)' not found in PATH"; \
		exit 1; \
	}
	@if [ -f "$(SOURCE_DIR)/$@.mtc" ]; then \
		mkdir -p "$(BUILD_DIR)"; \
		$(MTC) "$(SOURCE_DIR)/$@.mtc" "$(BUILD_DIR)/$@"; \
		"./$(BUILD_DIR)/$@"; \
	else \
		echo "Error: no rule to make target '$@'"; \
		exit 2; \
	fi

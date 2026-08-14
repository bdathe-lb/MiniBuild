# Compiler and basic compilation options
CXX           := g++
BASE_CXXFLAGS := -Wall -Wextra -pedantic -std=c++17 -pthread
LDFLAGS       :=

# Build mode: release or debug
MODE ?= release

ifeq ($(MODE), debug)
	CXXFLAGS := $(BASE_CXXFLAGS) \
							-g \
							-O0 \
							-fno-omit-frame-pointer \
							-fsanitize=address \
							-DDEBUG
	DIR_SUFFIX := debug
else ifeq ($(MODE), tsan)
	CXXFLAGS = $(BASE_CXXFLAGS) \
						 -g \
						 -O1 \
						 -fno-omit-frame-pointer \
						 -fsanitize=thread \
						 -DDEBUG
	DIR_SUFFIX := tsan
else
	CXXFLAGS := $(BASE_CXXFLAGS) \
							-O2 \
							-DNDEBUG
endif

# Directory setup
SRC_DIR    := src
INC_DIR    := include
TEST_DIR   := tests
BUILD_DIR  := build/$(DIR_SUFFIX)
BIN_DIR    := bin/$(DIR_SUFFIX)

# Application target
TARGET      := $(BIN_DIR)/minibuild
TEST_TARGET := $(BIN_DIR)/minibuild_tests

# Application sources
APP_SRCS  := $(wildcard $(SRC_DIR)/*.cpp)
CORE_SRCS := $(filter-out $(SRC_DIR)/main.cpp,$(APP_SRCS))

APP_OBJS  := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/src/%.o,$(APP_SRCS))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/src/%.o,$(CORE_SRCS))

# Test framework and test cases
TEST_FRAMEWORK_SRCS := \
	$(TEST_DIR)/test_framework.cpp \
	$(TEST_DIR)/test_main.cpp

TEST_CASE_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_SRCS      := $(TEST_FRAMEWORK_SRCS) $(TEST_CASE_SRCS)
TEST_OBJS      := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))

INC_FLAGS := -I$(INC_DIR) -I$(TEST_DIR)
DEPS      := $(sort $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d))

# -------------------- Public targets --------------------

all: $(TARGET)

run: $(TARGET)
	@$(TARGET)

test: $(TEST_TARGET)
	@$(TEST_TARGET)

test-debug:
	@$(MAKE) MODE=debug test

test-tsan:
	@$(MAKE) MODE=tsan test

# -------------------- Link rules --------------------

$(TARGET): $(APP_OBJS) | dirs
	@echo "Linking $@ (Mode: $(MODE))"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS) $(CORE_OBJS) | dirs
	@echo "Linking $@ (Mode: $(MODE))"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# -------------------- Compile rules --------------------

$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.cpp | dirs
	@echo "Compiling $< (Mode: $(MODE))"
	@$(CXX) $(CXXFLAGS) $(INC_FLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.cpp | dirs
	@echo "Compiling $< (Mode: $(MODE))"
	@$(CXX) $(CXXFLAGS) $(INC_FLAGS) -MMD -MP -c $< -o $@

# -------------------- Utility rules --------------------

dirs:
	@mkdir -p $(BUILD_DIR)/src $(BUILD_DIR)/tests $(BIN_DIR)

clean:
	@echo "Cleaning up $(MODE) build..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

clean-all:
	@echo "Cleaning up ALL builds..."
	@rm -rf build/ bin/

-include $(DEPS)

.PHONY: all run test test-debug test-tsan clean clean-all dirs

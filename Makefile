CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O2 -Wall -Wextra -std=gnu++98
CFLAGS ?= -O2 -Wall -Wextra -std=c99
CPPFLAGS ?=
CPPFLAGS += -Isrc -Icore

LDFLAGS ?=
LIBS = -lpcap -lpthread

OBJ_DIR := build/obj
SRC_DIR := src
BIN_DIR := bin
TOOLS_DIR := tools

SERVER_TARGET := $(BIN_DIR)/rxtracenetcap
SERVER_SRCS := main.cpp \
      rxhttpthread.cpp \
      rxhttpserver.cpp \
      rxserverconfig.cpp \
      rxsamplethread.cpp \
      rxcapturethread.cpp \
      rxcapturesession.cpp \
      rxstorageutils.cpp \
      rxcleanupthread.cpp \
      rxhttpresdataprocess.cpp \
      rxurlhandlers.cpp \
      rxprocdata.cpp \
      rxcapturemanagerthread.cpp \
      rxstrategyconfig.cpp \
      rxprocessresolver.cpp \
      rxreloadthread.cpp \
      rxfilterthread.cpp \
      rxlockfreequeue.c
LEGACY_DIR := core
LEGACY_SRCS := \
    $(LEGACY_DIR)/legacy_core_common.cpp \
    $(LEGACY_DIR)/legacy_core_net.cpp

SERVER_SRCS_FULL := $(addprefix $(SRC_DIR)/,$(SERVER_SRCS)) $(LEGACY_SRCS)

CLI_TARGET := $(BIN_DIR)/rxcli
CLI_SRC := $(TOOLS_DIR)/rxcli_main.cpp

# Protocol filter module
PDEF_DIR := src/pdef
RUNTIME_DIR := src/runtime
PDEF_SRCS := $(PDEF_DIR)/pdef_types.c \
             $(PDEF_DIR)/lexer.c \
             $(PDEF_DIR)/parser.c \
             $(RUNTIME_DIR)/executor.c \
             $(RUNTIME_DIR)/protocol.c

PDEF_OBJS := $(addprefix $(OBJ_DIR)/,$(PDEF_SRCS:.c=.o))

# C++ wrapper
PDEF_WRAPPER_SRC := $(PDEF_DIR)/pdef_wrapper.cpp
PDEF_WRAPPER_OBJ := $(addprefix $(OBJ_DIR)/,$(PDEF_WRAPPER_SRC:.cpp=.o))

# Test targets
TEST_TARGET := $(BIN_DIR)/test_pdef
TEST_SRC := tests/test_pdef.c

DEBUG_PARSE_TARGET := $(BIN_DIR)/debug_parse
DEBUG_PARSE_SRC := tests/debug_parse.c

TEST_DISASM_TARGET := $(BIN_DIR)/test_disasm
TEST_DISASM_SRC := tests/test_filter_disasm.c

INTEGRATION_EXAMPLE_TARGET := $(BIN_DIR)/integration_example
INTEGRATION_EXAMPLE_SRC := tests/integration_example.cpp

.PHONY: all clean directories server cli pdef test tools

all: directories pdef server cli test tools

server: $(SERVER_TARGET)

cli: $(CLI_TARGET)

directories:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

$(SERVER_TARGET): $(SERVER_SRCS_FULL) $(PDEF_LIB) | directories
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SERVER_SRCS_FULL) -L$(BIN_DIR) -lpdef $(LDFLAGS) $(LIBS)

$(CLI_TARGET): $(CLI_SRC) | directories
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

# Protocol filter library (static)
PDEF_LIB := $(BIN_DIR)/libpdef.a

$(PDEF_LIB): $(PDEF_OBJS) | directories
	ar rcs $@ $^

pdef: $(PDEF_LIB)

# Compile .c files to .o
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# Compile .cpp files to .o
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

# Test program
$(TEST_TARGET): $(TEST_SRC) $(PDEF_LIB) | directories
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BIN_DIR) -lpdef

test: $(TEST_TARGET)

# Debug tools
$(DEBUG_PARSE_TARGET): $(DEBUG_PARSE_SRC) $(PDEF_LIB) | directories
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BIN_DIR) -lpdef

$(TEST_DISASM_TARGET): $(TEST_DISASM_SRC) $(PDEF_LIB) | directories
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< -L$(BIN_DIR) -lpdef

# Integration example (C++)
$(INTEGRATION_EXAMPLE_TARGET): $(INTEGRATION_EXAMPLE_SRC) $(PDEF_WRAPPER_OBJ) $(PDEF_LIB) | directories
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $< $(PDEF_WRAPPER_OBJ) -L$(BIN_DIR) -lpdef

tools: $(DEBUG_PARSE_TARGET) $(TEST_DISASM_TARGET) $(INTEGRATION_EXAMPLE_TARGET)

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)
	find src -name '*.o' -delete
	find tests -name '*.o' -delete

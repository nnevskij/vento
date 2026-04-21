# --- Variables ---
CC = gcc
# Compiler flags:
# -Wall -Wextra = Show all warnings (crucial for catching bugs early)
# -Iinclude     = Tell GCC where to look for our .h files
CFLAGS = -Wall -Wextra -Iinclude

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Files to compile (automatically finds all .c files in the src folder)
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# Transforms names from src/file.c to obj/file.o (pre-assembled object files)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# The final executable name
TARGET = $(BIN_DIR)/vento

# --- Rules ---

# 'all' is the default rule triggered when running 'make'
all: directories $(TARGET)

# How to create the final executable (Linker)
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Build completed! Executable ready at $(TARGET)"

# How to compile individual .c files into .o files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create necessary directories if they don't exist
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

# Rule to clean the project (removes old binaries and objects)
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Project cleaned."
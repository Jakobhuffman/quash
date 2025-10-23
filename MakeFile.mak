# Makefile for Quash Shell

# The C compiler to use
CC = gcc
# C flags: -Wall (Warnings), -g (Debug info), -std=c99 (C Standard)
CFLAGS = -Wall -g -std=c99
# Object files list
OBJS = quash.o comand_par.o builtin_comands.o job_control.o
# Executable name
TARGET = quash

# Default target: builds the executable
all: $(TARGET)

# Rule to link the object files into the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) 

# Generic rule to compile .c files into .o files
# $@ is the target file (.o), $< is the first prerequisite (.c)
%.o: %.c quash.h
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up generated files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets don't correspond to files
.PHONY: all clean
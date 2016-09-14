rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
ALL_C_OBJ := $(call rwildcard,build-magenta-qemu-x86-64/,*.c.o)
ALL_CPP_OBJ := $(call rwildcard,build-magenta-qemu-x86-64/,*.cpp.o)
ALL_OBJ := $(ALL_C_OBJ) $(ALL_CPP_OBJ)

target: FORCE
	llvm-link $(ALL_OBJ) -o all.o
FORCE:

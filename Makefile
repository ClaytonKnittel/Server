CFLAGS=-c -O2 # -pthread
SRC=$(wildcard *.c)
OBJ=$(patsubst %.c,.obj/%.o,$(SRC))
DEP=$(wildcard *.h)
CC=gcc -MMD -MP
EXE=srv

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(OBJ) -o $@

.obj/%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

-include $(wildcard .obj/*.d)

.PHONY : clean
clean:
	rm -f .obj/*.o .obj/*.d $(EXE)

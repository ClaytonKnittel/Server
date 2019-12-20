CFLAGS=-c -O2 -std=c99 -pthread

MAIN=main
TEST_FOLDER=test

SRC=$(wildcard *.c)
OBJ=$(patsubst %.c,.obj/%.o,$(SRC))
DEP=$(wildcard *.h)

TSRC=$(wildcard $(TEST_FOLDER)/*.c)
TOBJ=$(patsubst $(TEST_FOLDER)/%.c,.obj/$(TEST_FOLDER)/%.o,$(TSRC))
TDEP=$(wildcard $(TEST_FOLDER)/*.h)
TEXES=$(patsubst %.c,%,$(TSRC))

OBJ_DEP=$(filter-out .obj/$(MAIN).o, $(OBJ))


CC=gcc -MMD -MP
EXE=srv

$(shell mkdir -p .obj)
$(shell mkdir -p .obj/test)

.PHONY: all
all: $(EXE) $(TEST_FOLDER)

$(TEST_FOLDER): $(TEXES)

$(EXE): $(OBJ)
	$(CC) $(OBJ) -o $@

.obj/%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

$(TEXES): $(TEST_FOLDER)/% : .obj/$(TEST_FOLDER)/%.o $(OBJ_DEP)
	$(CC) $< $(OBJ_DEP) -o $@

-include $(wildcard .obj/*.d)

.PHONY: clean
clean:
	rm -f .obj/*.o .obj/*.d .obj/$(TEST_FOLDER)/*.o .obj/$(TEST_FOLDER)/*.d $(EXE) $(TEXES)

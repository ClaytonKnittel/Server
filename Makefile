UNAME=$(shell uname -s)
ifeq ($(UNAME),Linux)
	LIBS=-pthread -lrt
	FEAT_TEST_MACROS=-D_GNU_SOURCE -D_POSIX_SOURCE
else ifeq ($(UNAME),Darwin)
	LIBS=-pthread
	FEAT_TEST_MACROS=
else
	$(error Not compatible for $(UNAME) systems)
endif

CFLAGS=-c -g -Wall -O0 -std=c99 -DDEBUG $(FEAT_TEST_MACROS)

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
	$(CC) $(OBJ) -o $@ $(LIBS)

.obj/%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

$(TEXES): $(TEST_FOLDER)/% : .obj/$(TEST_FOLDER)/%.o $(OBJ_DEP)
	$(CC) $< $(OBJ_DEP) -o $@ $(LIBS)

-include $(wildcard .obj/*.d)

.PHONY: clean
clean:
	rm -f .obj/*.o .obj/*.d .obj/$(TEST_FOLDER)/*.o .obj/$(TEST_FOLDER)/*.d $(EXE) $(TEXES)

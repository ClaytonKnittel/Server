UNAME=$(shell uname -s)
ifeq ($(UNAME),Linux)
	LIBS=-pthread -lrt -lm
	FEAT_TEST_MACROS=-D_DEFAULT_SOURCE -D_POSIX_SOURCE -D_GNU_SOURCE
else ifeq ($(UNAME),Darwin)
	LIBS=-pthread
	FEAT_TEST_MACROS=-D_GNU_SOURCE
else
	$(error Not compatible for $(UNAME) systems)
endif

ifeq ($(USER),lilching)
	# then we are on the AWS instance
	DIR="/home/lilching/public"
else
	DIR="$(shell pwd)/serveup"
endif

CFLAGS=-c -g -Wall -O0 -std=c99 -DDEBUG -D__USER__=\"$(USER)\" -DPUBLIC_FILE_SRC=\"$(DIR)\" $(FEAT_TEST_MACROS)

# name of c file containing main method
MAIN=main
SDIR=src
ODIR=.obj
TEST_FOLDER=test


SRC=$(shell find $(SDIR) -type f -name '*.c')
OBJ=$(patsubst $(SDIR)/%.c,$(ODIR)/%.o,$(SRC))
DEP=$(wildcard $(SDIR)/*.h)

TSRC=$(wildcard $(TEST_FOLDER)/*.c)
TOBJ=$(patsubst $(TEST_FOLDER)/%.c,$(ODIR)/$(TEST_FOLDER)/%.o,$(TSRC))
TDEP=$(wildcard $(TEST_FOLDER)/*.h)
TEXES=$(patsubst %.c,%,$(TSRC))

OBJ_DEP=$(filter-out $(ODIR)/$(MAIN).o, $(OBJ))


CC=gcc -MMD -MP
EXE=srv

DIRS=$(shell find $(SDIR) -type d)
OBJDIRS=$(patsubst $(SDIR)/%,$(ODIR)/%,$(DIRS))

$(shell mkdir -p $(ODIR))
$(shell mkdir -p $(OBJDIRS))
$(shell mkdir -p $(ODIR)/test)

.PHONY: all
all: $(EXE) $(TEST_FOLDER)

$(TEST_FOLDER): $(TEXES)

$(EXE): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LIBS)

$(ODIR)/%.o: $(SDIR)/%.c
	$(CC) $(CFLAGS) $< -o $@

$(TEXES): $(TEST_FOLDER)/% : $(ODIR)/$(TEST_FOLDER)/%.o $(OBJ_DEP)
	$(CC) $< $(OBJ_DEP) -o $@ $(LIBS)

$(ODIR)/$(TEST_FOLDER)/%.o: $(TEST_FOLDER)/%.c
	$(CC) $(CFLAGS) $< -o $@

-include $(wildcard .obj/*.d)

.PHONY: clean
clean:
	rm -f $(ODIR)/*.o $(ODIR)/*.d $(ODIR)/$(TEST_FOLDER)/*.o $(ODIR)/$(TEST_FOLDER)/*.d $(EXE) $(TEXES)

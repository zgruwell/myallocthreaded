# specify all source files here
SRCS = alligator.c myalloc.c

# specify targets here (names of executables)
TARG = alligator

# specify compiler, flags and libs here
CC = gcc
OPTS = -Wall -O
OBJS = $(SRCS:.c=.o)
LIBS = -lm -lpthread # placeholder - not needed for forksort

# 'all' is not really needed
all: $(TARG)

# generates target executable
$(TARG): $(OBJS)
	$(CC) -o $(TARG) $(OBJS) $(LIBS)

# generates all object files from matching c files
%.o: %.c
	$(CC) $(OPTS) -c $< -o $@

# cleans up stuff
clean:
	rm -f $(OBJS) $(TARG) *~ *.sorted log/* correct/*

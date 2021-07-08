CC=gcc
OBJS = s2dsm.o
OPTIONS = -g -Wall -pthread
.PHONY: clean

SRC = $(wildcard *.c)

EXE = $(SRC:.c=)

s2dsm: ${OBJS}
	${CC} ${OPTIONS} $< -o $@

%.o:%.c
	${CC} ${OPTIONS} -c $<

clean:
	rm -rf ${OBJS}
	rm -rf ${EXE}

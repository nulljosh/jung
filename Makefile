CC = cc
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic
SRCS = src/main.c src/lexer.c src/parser.c src/value.c src/table.c src/interpreter.c src/builtins.c
TARGET = jung

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

clean:
	rm -f $(TARGET)

.PHONY: clean

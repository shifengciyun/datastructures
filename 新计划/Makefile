# 自习室预约管理系统 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99
TARGET = study_room
SRC = main.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)

clean:
	rm -f $(TARGET) $(TARGET).exe

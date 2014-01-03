CC = gcc
CFLAGS = -Wall -pedantic
LDFLAGS+= -pthread
BIN = ups-server
OBJ = queue.o err.o global.o logger.o client.o server.o sender.o receiver.o game.o game_watchdog.o com.o main.o

%.o: %.c
	$(CC) -c $(LDFLAGS) $(CFLAGS) $< -o $@

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -rf *.o $(BIN)

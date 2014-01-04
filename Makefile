CC = gcc
CFLAGS = -Wall -pedantic
LDFLAGS += -pthread -lm
BIN = cns_server
OBJ = queue.o err.o global.o logger.o client.o server.o sender.o receiver.o game.o game_watchdog.o com.o main.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN): $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf *.o $(BIN)

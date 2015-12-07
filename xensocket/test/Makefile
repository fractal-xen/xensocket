all: sender receiver

sender: sender.c
	gcc -Wall -o sender sender.c

receiver: receiver.c
	gcc -Wall -o receiver receiver.c

clean:
	rm -f sender receiver *.o *~

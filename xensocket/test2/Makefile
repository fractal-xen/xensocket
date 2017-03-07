all: sender receiver

sender: sender.c
	gcc -Wall -g -o sender sender.c

receiver: receiver.c
	gcc -Wall -g -o receiver receiver.c

clean:
	rm -f sender receiver *.o *~

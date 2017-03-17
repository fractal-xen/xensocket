all: sender receiver transceiver

sender: sender.c
	gcc -Wall -g -o sender sender.c

receiver: receiver.c
	gcc -Wall -g -o receiver receiver.c

transceiver: transceiver.c
	gcc -Wall -g -o transceiver transceiver.c

clean:
	rm -f sender receiver transceiver *.o *~

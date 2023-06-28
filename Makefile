CC = gcc
CFLAGS = -Wall -pedantic -g 


all: mush2


mush2: mush2.o
  $(CC) $(CFLAGS) -o mush2 -L ~pn-cs357/Given/Mush/lib64 mush2.o -lmush
mush2.o: mush2.c
  $(CC) $(CFLAGS) -c -o mush2.o -I ~pn-cs357/Given/Mush/include mush2.c


clean:
	rm -f *.o

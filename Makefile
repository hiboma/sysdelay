sysdelay: sysdelay.c sysdelay.h
	$(CC) -Wall -Werror -lpthread $@.c -o $@

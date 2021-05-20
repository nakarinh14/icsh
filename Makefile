all: icsh.c 
	gcc -o icsh icsh.c

clean: 
	$(RM) icsh
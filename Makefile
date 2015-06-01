SHELL=C:/Windows/System32/cmd.exe
objects = main.o wfLZ.o
LIBPATH = -L./lib
LIB = -lsquish ./lib/FreeImage.lib
HEADERPATH = -I./include
STATICGCC = -static-libgcc -static-libstdc++

all : sk_decomp.exe
 
sk_decomp.exe : $(objects)
	g++ -Wall -O2 -o $@ $(objects) $(LIBPATH) $(LIB) $(STATICGCC) $(HEADERPATH)
	
%.o: %.cpp
	g++ -O2 -g -ggdb -c -MMD -o $@ $< $(HEADERPATH)

-include $(objects:.o=.d)

.PHONY : clean
clean :
	rm -rf sk_decomp.exe *.o *.d

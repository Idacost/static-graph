CXXFLAGS=-I /mnt/c/Users/idaco/Documents/prgs/rapidjson/include
LDFLAGS=-lcurl
LD=g++
CC=g++

all: level_client

level_client: level_client.o
	$(LD) $< -o $@ $(LDFLAGS)

level_client.o: level_client.cpp
	$(CC) $(CXXFLAGS) -c level_client.cpp

clean:
	-rm level_client level_client.o

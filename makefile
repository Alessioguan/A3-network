CXX = g++
CXXFLAGS = -lpthread

# Targets
.PHONY: all clean

all: shfd client

shfd: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o shfd

client: client.cpp
	$(CXX) client.cpp -o client

clean:
	rm -f shfd client

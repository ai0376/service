CXX=g++

CFLAGS = $(shell pkg-config --cflags seastar)
LIBS = $(shell pkg-config --libs --static seastar)

CFLAGS += -I./deps/redisclient/src
LIBS += -L./deps/redisclient/src/redisclient

LIBS += -lRedisClient

SRC=./src

service: $(SRC)/main.cpp
	$(CXX) $^ $(CFLAGS) $(LIBS) -o $@


.PHONY: clean

clean:
	-rm  service

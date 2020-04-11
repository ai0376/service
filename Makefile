CXX=g++

CFLAGS = $(shell pkg-config --cflags seastar)
LIBS = $(shell pkg-config --libs --static seastar)

SRC=./src

service: $(SRC)/main.cpp
	$(CXX) $^ $(CFLAGS) $(LIBS) -o $@


.PHONY: clean

clean:
	-rm  main

CXXFLAGS=-std=c++17 -g -Wall $(shell pkg-config --cflags libcurl)
LDLIBS += $(shell pkg-config --libs libcurl)

mailrss: tinyxml2.o mailrss.cpp
	$(CXX) $(CXXFLAGS) $(LDLIBS) -o mailrss.o -c mailrss.cpp
	$(CXX) $(CXXFLAGS) $(LDLIBS) -o mailrss mailrss.o tinyxml2.o

tinyxml2.o: lib/tinyxml2/tinyxml2.cpp lib/tinyxml2/tinyxml2.h
	$(CXX) -o $@ -c $<

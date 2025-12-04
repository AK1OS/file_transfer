CXX = g++
CXXFLAGS = -pthread -Wall -O2 -Wno-unused-result

CLIENT_SOURCES = client/main_client.cpp client/interactive_tcp_client.cpp \
                client/transfer_handlers.cpp client/network_utils.cpp
SERVER_SOURCES = server/main_server.cpp server/interactive_tcp_server.cpp \
                server/session_manager.cpp server/transfer_handlers.cpp \
                server/network_utils.cpp

CLIENT_OBJS = $(CLIENT_SOURCES:.cpp=.o)
SERVER_OBJS = $(SERVER_SOURCES:.cpp=.o)

CLIENT_TARGET = file_transfer_client
SERVER_TARGET = file_transfer_server

all: $(CLIENT_TARGET) $(SERVER_TARGET)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(CLIENT_OBJS) $(SERVER_OBJS) $(CLIENT_TARGET) $(SERVER_TARGET)

.PHONY: all clean
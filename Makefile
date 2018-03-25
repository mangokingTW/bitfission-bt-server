all:
	g++ bt_server.c -o bt_server -ltorrent-rasterbar -lboost_system -lstdc++ -lm -lgcc -lssl -lcrypto -ldl -lpthread 
static:
	g++ bt_server.c -o bt_server -ltorrent-rasterbar -lboost_system -lstdc++ -lm -lgcc -lssl -lcrypto -ldl -lpthread -static

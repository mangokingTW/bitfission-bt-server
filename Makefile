all:
	g++ bt_server.c -o bt_server -ltorrent-rasterbar -lboost_system -lstdc++ -lm -lgcc -lssl -lcrypto -lboost_chrono -lboost_random -ldl -lpthread 

all:
	g++ bt_server.c ../src/partclone.c ../src/checksum.c -I ../ -I ../src -DLOCALEDIR=\"/usr/share/locale\" -o bt_server -fpermissive -lgcc -lm -ltorrent-rasterbar -lboost_system -lstdc++ -lssl -lcrypto -lboost_chrono -lboost_random -ldl -lpthread

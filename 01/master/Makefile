TARGET=master


all:
	g++ main.cpp http_client.cpp ../mongoose/mongoose.c \
	-I../mongoose \
	-lsqlite3 \
	-o master



clean:
	rm -f $(TARGET)
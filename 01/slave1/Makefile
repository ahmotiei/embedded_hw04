TARGET=slave1


all:
	g++ main.cpp ../mongoose/mongoose.c \
	-I../mongoose \
	-lsqlite3 \
	-o $(TARGET)



clean:
	rm -f $(TARGET)
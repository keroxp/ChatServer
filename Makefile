all:
	make client
	make server
server: ChatServer/main.c
	clang ChatServer/main.c -o server 
client: ChatClient/main.c
	clang ChatClient/main.c -o client -lncurses
parser: msg_parser.c
	clang msg_parser.c -o parser

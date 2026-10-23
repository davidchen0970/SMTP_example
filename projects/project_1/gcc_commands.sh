gcc -g -Wall test_client.c \
    -o test_client \
    -lesmtp -lssl -lcrypto

g++ -std=c++17 -g -Wall test_client.cpp \
    -o test_client_cpp \
    -lesmtp -lssl -lcrypto -lpthread
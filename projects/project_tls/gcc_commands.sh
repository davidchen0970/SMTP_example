gcc -g -Wall test_client_tls.c \
    -o test_client_tls \
    -lesmtp -lssl -lcrypto

g++ -std=c++17 -g -Wall test_client_tls.cpp \
    -o test_client_tls_cpp \
    -lesmtp -lssl -lcrypto -lpthread
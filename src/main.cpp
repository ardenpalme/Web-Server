#include<iostream>
#include<fstream>
#include<string>
#include<thread>
#include<chrono>

#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "csapp.h"
#include "client.hpp"

void handle_client(shared_ptr<ClientHandler> cli_hndl);

void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX *create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, "/etc/ssl/certs/www_ardenpalme_com.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "/etc/ssl/private/custom.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // Ensure the private key is valid
    if (!SSL_CTX_check_private_key(ctx)) {
        cerr << "Private key does not match the public certificate" << endl;
        exit(EXIT_FAILURE);
    }

    // Enforce TLSv1.2
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

}

void set_socket_timeout(int sockfd, int seconds) {
    struct timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    
    // Set socket read/write timeouts
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int main(int argc, char *argv[])
{
    uint16_t srv_port = 443;
    std::string srv_port_str = std::to_string((int)srv_port);

    init_openssl();
    SSL_CTX *ctx = create_context();
    configure_context(ctx);

    int listenfd = Open_listenfd((char*)srv_port_str.c_str());

    struct sockaddr_storage addr;
    socklen_t msg_len = sizeof(struct sockaddr_storage);
    int ret;
    
    while(1) {
        char cli_name[100];
        char cli_port[100];
        int connfd = Accept(listenfd, (struct sockaddr*)&addr, &msg_len);
        Getnameinfo((struct sockaddr*)&addr, msg_len, cli_name, 
            100, cli_port, 100, NI_NUMERICHOST | NI_NUMERICSERV);

        SSL *ssl = SSL_new(ctx);
        if(!ssl){
            cout << "SSL is NULL!\n";
            Close(connfd);
            continue;
        } 
        if((ret=SSL_set_fd(ssl, connfd)) < 0){
            cerr << "SSL error during setting fd: " << ERR_error_string(ERR_get_error(), nullptr) << endl;
            SSL_free(ssl);
            Close(connfd);
            continue;
        }

        if ((ret = SSL_accept(ssl)) <= 0) {
            int ssl_err = SSL_get_error(ssl, ret);
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                cerr << "retry SSL_accept" <<endl;
                continue;
            }else{
                cerr << "SSL accept error: " << ERR_error_string(ERR_get_error(), nullptr) << endl;
                SSL_free(ssl);
                Close(connfd);
                continue;
            }
        } 

        auto cli_hndl_ptr = std::make_shared<ClientHandler>(connfd, ssl);
        std::thread cli_thread(handle_client, cli_hndl_ptr);
        cli_thread.detach();
    }
    return 0;
}
void handle_client(shared_ptr<ClientHandler> cli_hndl) {
    cli_err err;
    err = cli_hndl->parse_request();
    while(err == cli_err::RETRY) {
        err = cli_hndl->parse_request();
    }

    if (err == cli_err::PARSE_ERROR) {
        cerr << "Request parsing failed." << endl;
        cli_hndl->cleanup();
        return;

    } else if (err == cli_err::CLI_CLOSED_CONN) {
        cerr << "Client closed connection during request parsing." << endl;
        cli_hndl->cleanup();
        return;

    } else if (err != cli_err::NONE) {
        cerr << "Error during request parsing." << endl;
        cli_hndl->cleanup();
        return;
    }

    if((err=cli_hndl->serve_client()) != cli_err::NONE) {
        cerr << "Error serving client request." << endl;
        cli_hndl->cleanup();
        return;
    }

    cli_hndl->cleanup();
}
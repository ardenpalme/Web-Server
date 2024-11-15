#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <memory>

#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "util.hpp"
#include "client.hpp"

class ServerContext {
public:
    ServerContext(int _port) : port{_port} {
        init_openssl();
        ctx = create_context();
        configure_context(ctx);
        cache = make_shared<Cache>();
    }

    void enqueue_client(shared_ptr<ClientHandler> cli_hndl_ptr) {
        std::unique_lock lck {client_queue_mtx};
        active_clients.push_back(cli_hndl_ptr);
    }

    void dequeue_client(shared_ptr<ClientHandler> cli_hndl_ptr) {
        std::unique_lock lck {client_queue_mtx};

        auto iter = find(active_clients.begin(), active_clients.end(), cli_hndl_ptr);
        if(iter != active_clients.end()) {
            active_clients.erase(iter);
        }
    }

    SSL *make_ssl(void) {
        SSL *ssl = SSL_new(ctx);
        return ssl;
    }

    shared_ptr<Cache> get_cache(void) {
        return cache;
    }

private:
    int port;
    SSL_CTX *ctx;
    vector<shared_ptr<ClientHandler>> active_clients;
    mutex client_queue_mtx;
    shared_ptr<Cache> cache;

    void init_openssl();

    void cleanup_openssl();

    SSL_CTX *create_context();

    void configure_context(SSL_CTX *ctx);
};

#endif /* __SERVER_HPP__ */
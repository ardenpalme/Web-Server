#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "client.hpp"
#include "util.hpp"

#define MAX_NUM_HEADERS 120
#define MAX_FILESIZE (10 * 1024)

using namespace std;

void handle_client(ClientHandler &hndl) {
    cli_err err;
    err = hndl.parse_request();
    while(err == cli_err::RETRY) {
        err = hndl.parse_request();
    }

    if (err == cli_err::PARSE_ERROR) {
        cerr << "Request parsing failed." << endl;
        hndl.cleanup();
        return;

    } else if (err == cli_err::CLI_CLOSED_CONN) {
        cerr << "Client closed connection during request parsing." << endl;
        hndl.cleanup();
        return;

    } else if (err != cli_err::NONE) {
        cerr << "Error during request parsing." << endl;
        hndl.cleanup();
        return;
    }

    if((err=hndl.serve_client()) != cli_err::NONE) {
        cerr << "Error serving client request." << endl;
        hndl.cleanup();
        return;
    }

    hndl.cleanup();
}

cli_err ClientHandler::serve_client(void) {
    if (request_line.size() < 3) {
        cerr << "Invalid request line format." << endl;
        return cli_err::SERVE_ERROR;
    }

    string method = request_line[0];
    string uri = request_line[1];
    string protocol = request_line[2];

    if(method == "GET") {
        if(uri == "/") {
            uri = "index.html";
        }else{ 
            uri = uri.substr(1);
        }
        uri.insert(0, "data/");
        try {
            serve_static(uri);
        } catch (const exception &e) {
            cerr << "Error serving static content: " << e.what() << endl;
            return cli_err::SERVE_ERROR;
        }
    } else {
        cerr << "Unsupported HTTP method: " << method << endl;
        return cli_err::SERVE_ERROR;
    }
    return cli_err::NONE;
}

void ClientHandler::serve_static(string filename) {
    char filetype[MAXLINE], buf[MAXLINE];

    ifstream ifs(filename, std::ifstream::binary);
    if(!ifs) {
        cerr << filename << " non found." << endl;
        return;
    }

    std::filebuf* pbuf = ifs.rdbuf();
    std::size_t file_size = pbuf->pubseekoff (0,ifs.end,ifs.in);
    pbuf->pubseekpos (0,ifs.in);

    char* file_buf=new char[file_size];
    pbuf->sgetn (file_buf,file_size);
    ifs.close();

    lock_guard<mutex> lock(*ssl_mutex);
    /* Send response headers to client */
    get_filetype((char*)filename.c_str(), filetype);    //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); //line:netp:servestatic:beginserve
    SSL_write(ssl, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    SSL_write(ssl, buf, strlen(buf));
    sprintf(buf, "Content-length: %lu\r\n", file_size);
    SSL_write(ssl, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    SSL_write(ssl, buf, strlen(buf));

    SSL_write(ssl, file_buf, file_size);

    delete[] file_buf;
}

cli_err ClientHandler::parse_request() {
    char *raw_req = new char[MAX_FILESIZE];

    ssl_mutex->lock();
    int bytes_read = SSL_read(ssl, raw_req, MAX_FILESIZE);
    ssl_mutex->unlock();

    if (bytes_read <= 0) {
        int ssl_error = SSL_get_error(ssl, bytes_read);
        
        switch (ssl_error) {
            case SSL_ERROR_ZERO_RETURN:
                cerr << "Client closed connection." << endl;
                delete[] raw_req;
                return cli_err::CLI_CLOSED_CONN;

            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                cerr << "SSL read wants retry." << endl;
                delete[] raw_req;
                return cli_err::RETRY;

            default:
                cerr << "SSL read error: " << ERR_error_string(ERR_get_error(), nullptr) << endl;
                delete[] raw_req;
                return cli_err::FATAL;
        }
    }

    string req_str(raw_req);

    vector<string> request_lines = splitline(req_str, '\r');

    if (request_lines.empty()) {
        cerr << "Empty request received." << endl;
        return cli_err::PARSE_ERROR;
    }

    int line_ct = 0;
    for(auto line : request_lines){

        if(line == "\n" || line.empty()) break;

        if(line_ct == 0) {
            request_line = splitline(line, ' ');

        }else{
            line = line.substr(1); // remove the newline character at the beginning
            size_t sep_idx = line.find(':');
            if (sep_idx == string::npos || sep_idx == 0) {
                cerr << "Malformed header: " << line << endl;
                return cli_err::PARSE_ERROR;
            }

            request_hdrs.insert({line.substr(0, sep_idx), 
                                 line.substr(sep_idx+2)});
        }

        line_ct++;
    }
    return cli_err::NONE;
}

cli_err ClientHandler::cleanup() {
    lock_guard<mutex> lock(*ssl_mutex);
    if (SSL_shutdown(ssl) < 0) {
        cerr << "Error shutting down SSL: " << ERR_error_string(ERR_get_error(), nullptr) << endl;
        SSL_free(ssl);
        Close(connfd);
        return cli_err::CLEANUP_ERROR;
    }

    SSL_free(ssl);
    Close(connfd);
    return cli_err::NONE;
}

ostream &operator<<(ostream &os, ClientHandler &cli) {

    string method = cli.request_line[0];
    string uri = cli.request_line[1];
    string protocol = cli.request_line[2];

    cout << "method = [" << method << "]" << endl
         << "uri = [" << uri << "]" << endl
         << "protocol = [" << protocol << "]" << endl;

    return os;
}
/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <cassert>
#include <vector>
#include <thread>
#include <mutex>

#include "secure_socket.hh"
#include "certificate.hh"
#include "exception.hh"
#include "util.hh"

using namespace std;

/* error category for OpenSSL */
class ssl_error_category : public error_category
{
public:
    const char *name(void) const noexcept override { return "SSL"; }
    string message(const int ssl_error) const noexcept override
    {
        return ERR_error_string(ssl_error, nullptr);
    }
};

class ssl_error : public tagged_error
{
public:
    ssl_error(const string &s_attempt,
              const int error_code = ERR_get_error())
        : tagged_error(ssl_error_category(), s_attempt, error_code)
    {
        cout << "err code is " << error_code << '\n';
    }
};

class OpenSSL
{
private:
    // vector<mutex> locks_;

    // static void locking_function( int mode, int n, const char *, int )
    // {
    //     if ( mode & CRYPTO_LOCK ) {
    //         OpenSSL::global_context().locks_.at( n ).lock();
    //     } else {
    //         OpenSSL::global_context().locks_.at( n ).unlock();
    //     }
    // }

    static unsigned long id_function(void)
    {
        return pthread_self();
    }

public:
    OpenSSL()
    {
        /* SSL initialization: Needs to be done exactly once */
        /* load algorithms/ciphers */
        // SSL_library_init();
        // OpenSSL_add_all_algorithms();

        /* load error messages */
        // SSL_load_error_strings();

        /* set thread-safe callbacks */
        // CRYPTO_set_locking_callback( locking_function );
        // CRYPTO_set_id_callback( id_function );
    }

    static OpenSSL &global_context(void)
    {
        static OpenSSL os;
        return os;
    }
};

SSL_CTX *initialize_new_context(const SSL_MODE type)
{
    OpenSSL::global_context();
    SSL_CTX *ret = SSL_CTX_new(type == CLIENT ? TLS_client_method() : TLS_server_method());
    if (not ret)
    {
        throw ssl_error("SSL_CTL_new");
    }
    return ret;
}

SSLContext::SSLContext(const SSL_MODE type)
    : ctx_(initialize_new_context(type))
{
    if (type == SERVER)
    {
        if ( not SSL_CTX_use_certificate_ASN1( ctx_.get(), 678, certificate ) )
        {
            throw ssl_error("SSL_CTX_use_certificate_ASN1");
        }

        if ( not SSL_CTX_use_RSAPrivateKey_ASN1( ctx_.get(), private_key, 1191 ) )
        {
            throw ssl_error("SSL_CTX_use_RSAPrivateKey_ASN1");
        }

        /* check consistency of private key with loaded certificate */
        if (not SSL_CTX_check_private_key(ctx_.get()))
        {
            throw ssl_error("SSL_CTX_check_private_key");
        }
    }
}

SecureSocket::SecureSocket(TCPSocket &&sock, SSL *ssl)
    : TCPSocket(move(sock)),
      ssl_(ssl),
      isConnected(false)
{
    if (not ssl_)
    {
        throw runtime_error("SecureSocket: constructor must be passed valid SSL structure");
    }

    if (not SSL_set_fd(ssl_.get(), fd_num()))
    {
        throw ssl_error("SSL_set_fd");
    }

    /* enable read/write to return only after handshake/renegotiation and successful completion */
    SSL_set_mode(ssl_.get(), SSL_MODE_AUTO_RETRY);
}

SecureSocket SSLContext::new_secure_socket(TCPSocket &&sock)
{
    return SecureSocket(move(sock),
                        SSL_new(ctx_.get()));
}

void SecureSocket::set_host_name(const char *hostname)
{
    SSL_set_tlsext_host_name(ssl_.get(), hostname);
}

bool SecureSocket::is_connected(void)
{
    return isConnected;
}

void SecureSocket::connect(void)
{
    if (not SSL_connect(ssl_.get()))
    {
        throw ssl_error("SSL_connect");
    }
    isConnected = true;
}

void SecureSocket::accept(void)
{
    const auto ret = SSL_accept(ssl_.get());
    if (ret == 1)
    {
        isConnected = true;
        return;
    }
    else
    {
        throw ssl_error("SSL_accept");
    }
    register_read();
}

string SecureSocket::read(void)
{

    if (not is_connected())
    {
        connect();
    }
    /* SSL record max size is 16kB */
    const size_t SSL_max_record_length = 16384;

    char buffer[SSL_max_record_length];

    ssize_t bytes_read = SSL_read(ssl_.get(), buffer, SSL_max_record_length);

    /* Make sure that we really are reading from the underlying fd */
    assert(0 == SSL_pending(ssl_.get()));
    // cout << "SSL_read: SSL mode is " << sslmode << "\n";
    if (bytes_read == 0)
    {
        int error_return = SSL_get_error(ssl_.get(), bytes_read);
        if (SSL_ERROR_ZERO_RETURN == error_return)
        { /* Clean SSL close */
            set_eof();
        }
        else if (SSL_ERROR_SYSCALL == error_return)
        { /* Underlying TCP connection close */
            /* Verify error queue is empty so we can conclude it is EOF */
            assert(ERR_get_error() == 0);
            set_eof();
        }
        register_read();
        return string(); /* EOF */
    }
    else if (bytes_read < 0)
    {
        throw ssl_error("SSL_read");
    }
    else
    {
        /* success */
        register_read();
        return string(buffer, bytes_read);
    }
}

void SecureSocket::write(const string &message)
{
    /*
    Lazy connect: We initiate the TLS connection only after the hostname information is available
    The hostname info is available only after the first GET request from the client 
    */
    if (not is_connected())
    {
        string host_name = get_host_name(message);
        if (host_name != "")
        {
            set_host_name(host_name.c_str());
        }
        connect();
    }
    /* SSL_write returns with success if complete contents of message are written */
    ssize_t bytes_written = SSL_write(ssl_.get(), message.data(), message.length());
    if (bytes_written < 0)
    {
        int sslError = SSL_get_error(ssl_.get(), bytes_written);
        throw ssl_error("SSL_write", sslError);
    }

    register_write();
}

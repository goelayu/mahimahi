/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fstream>
#include <sstream>  
#include <iostream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <ctime>
#include <unistd.h>
#include "http_message.hh"
#include "exception.hh"
#include "http_record.pb.h"

using namespace std;

/* methods called by an external parser */
void HTTPMessage::set_first_line( const string & str )
{
    assert( state_ == FIRST_LINE_PENDING );
    first_line_ = str;
    state_ = HEADERS_PENDING;
}

void HTTPMessage::add_header( const std::string & str )
{
    // if (str.find("Content-Length") != std::string::npos) {
    //     return;
    // }
    cout << "str is " <<  str << endl;
    assert( state_ == HEADERS_PENDING );
    headers_.emplace_back( str );
}

void HTTPMessage::done_with_headers( void )
{
    assert( state_ == HEADERS_PENDING );
    state_ = BODY_PENDING;

    calculate_expected_body_size();
}

void HTTPMessage::set_expected_body_size( const bool is_known, const size_t value )
{
    assert( state_ == BODY_PENDING );
    
    expected_body_size_ = make_pair( is_known, value );
}

size_t HTTPMessage::read_in_body( const std::string & str )
{
    assert( state_ == BODY_PENDING );

    if ( body_size_is_known() ) {
        /* body size known in advance */

        assert( body_.size() <= expected_body_size() );
        const size_t amount_to_append = min( expected_body_size() - body_.size(),
                                             str.size() );

        body_.append( str.substr( 0, amount_to_append ) );
        if ( body_.size() == expected_body_size() ) {
            state_ = COMPLETE;
            rewrite_body(body_);
        }

        return amount_to_append;
    } else {
        /* body size not known in advance */
        return read_in_complex_body( str );
    }
}

void HTTPMessage::eof( void )
{
    switch ( state() ) {
    case FIRST_LINE_PENDING:
        return;
    case HEADERS_PENDING:
        throw runtime_error( "HTTPMessage: EOF received in middle of headers" );
    case BODY_PENDING:
        if ( eof_in_body() ) {
            state_ = COMPLETE;
        }
        break;
    case COMPLETE:
        assert( false ); /* nobody should be calling methods on a complete message */
        return;
    }
}

bool HTTPMessage::body_size_is_known( void ) const
{
    assert( state_ > HEADERS_PENDING );
    return expected_body_size_.first;
}

size_t HTTPMessage::expected_body_size( void ) const
{
    assert( body_size_is_known() );
    return expected_body_size_.second;
}

/* locale-insensitive ASCII conversion */
static char http_to_lower( char c )
{
    const char diff = 'A' - 'a';
    if ( c >= 'A' and c <= 'Z' ) {
        c -= diff;
    }
    return c;
}

static string strip_initial_whitespace( const string & str )
{
    size_t first_nonspace = str.find_first_not_of( " " );
    if ( first_nonspace == std::string::npos ) {
        return "";
    } else {
        return str.substr( first_nonspace );
    }
}

/* check if two strings are equivalent per HTTP 1.1 comparison (case-insensitive) */
bool HTTPMessage::equivalent_strings( const string & a, const string & b )
{
    const string new_a = strip_initial_whitespace( a ),
        new_b = strip_initial_whitespace( b );

    if ( new_a.size() != new_b.size() ) {
        return false;
    }

    for ( auto it_a = new_a.begin(), it_b = new_b.begin(); it_a < new_a.end(); it_a++, it_b++ ) {
        if ( http_to_lower( *it_a ) != http_to_lower( *it_b ) ) {
            return false;
        }
    }

    return true;
}

bool HTTPMessage::has_header( const string & header_name ) const
{
    for ( const auto & header : headers_ ) {
        /* canonicalize header name per RFC 2616 section 2.1 */
        if ( equivalent_strings( header.key(), header_name ) ) {
            return true;
        }
    }

    return false;
}

const string & HTTPMessage::get_header_value( const std::string & header_name ) const
{
    for ( const auto & header : headers_ ) {
        /* canonicalize header name per RFC 2616 section 2.1 */
        if ( equivalent_strings( header.key(), header_name ) ) {
            return header.value();
        }
    }

    throw runtime_error( "HTTPMessage header not found: " + header_name );
}

void HTTPMessage::update_header( const std::string & header_name, std::string val )
{
    // auto it;
    for (  auto & header : headers_ ) {
        /* canonicalize header name per RFC 2616 section 2.1 */
        if ( equivalent_strings( header.key(), header_name ) ) {
            header.set_value(val);
            // return header.value();
            // it = find(headers_.begin(), headers_.end(), header);
            // headers_.erase(std::remove(headers_.begin(), headers_.end(), header), headers_.end());
        }
    }

    // if (it != headers_.end()){
    //     // int index = it - headers_.begin();
    //     headers_.erase(it - headers_.begin());
    // }

    // throw runtime_error( "HTTPMessage header not found: " + header_name );
}

const string HTTPMessage::gen_random(const int len) const 
{
    
    string tmp_s;
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    srand( (unsigned) time(NULL) * getpid());

    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) 
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    
    
    return tmp_s;
    
}

void HTTPMessage::rewrite_body( std::string & body)
{
    std::string zip_type;
    std::string mime_type;
    for ( const auto & header : headers_ ) {
        /* canonicalize header name per RFC 2616 section 2.1 */
        if ( equivalent_strings( header.key(), "Content-Encoding" ) ) {
            zip_type = header.value();
        } else if ( equivalent_strings( header.key(), "Content-Type" ) ){
            mime_type = header.value();
        }
    }

    cout << "mime type " << mime_type << endl;
    cout << "zip type " << zip_type << endl;
    if (mime_type.find("html") == std::string::npos) {
        return;
    }

    if (!zip_type.empty()){
        /*Decompress the string before adding data */
        namespace bio = boost::iostreams;

		std::stringstream compressed(body);
		std::stringstream decompressed;

		bio::filtering_streambuf<bio::input> out;
		out.push(bio::gzip_decompressor());
		out.push(compressed);
		bio::copy(out, decompressed);

		body = decompressed.str();

    }

    body = "TESTING STRING " + body;
    
    if (!zip_type.empty()){
        /*Compress the string back again*/
        namespace bio = boost::iostreams;

        std::stringstream compressed;
		std::stringstream origin(body);

		bio::filtering_streambuf<bio::input> out;
		out.push(bio::gzip_compressor(bio::gzip_params(bio::gzip::best_compression)));
		out.push(origin);
		bio::copy(out, compressed);

		body = compressed.str();
    }

    // update content length
    
    // std::string content_length = "Content-Length: ";
    // content_length += std::to_string(body.size());
    update_header("Content-Length", std::to_string(body.size()));
    // headers_.emplace_back( content_length );

    return;

    std::string suffix;
    suffix = gen_random(5);
    cout << "rewriting html file " << endl;
    ofstream myfile;
    std::string infile;
    infile = "/home/goelayu/text" + suffix;
    std::string outfile;
    outfile = "/home/goelayu/text2" + suffix;
    if (!zip_type.empty()){
        myfile.open(infile, ios::binary | ios::app);
        myfile.write((char*)body.c_str(), body.size());
        myfile.close();
    } else {
        myfile.open(infile);
        myfile << body << endl;
        myfile.close();
    }

    // return;
    std::string cmd;
    ostringstream _cmd;
    cmd = "bash -c 'NODE_PATH=/home/goelayu/research/WebPeformance/node_modules /usr/local/bin/node /home/goelayu/research/webArchive/scripts/rewrite.js -i " + infile + " -o " +  outfile;
    if (!zip_type.empty()) {
        cmd = cmd + " -z'";
    } else {
        cmd = cmd + "'";
    }

    const char *command = cmd.c_str();
    // std::string str(command);
    cout << " command is " << cmd << endl;
    cout << first_line() << endl;
    int systemRet =  system(command);
    cout << systemRet << endl;

    // if (!zip_type.empty()){
    //     std::ifstream input( "/home/goelayu/text2", std::ios::binary );
    //     std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(input), {});
    //     std::string s(buffer.begin(), buffer.end());
    //     body = s;
    // } else {
    //     std::ifstream file("/home/goelayu/text", std::ios::binary | std::ios::ate);
    //     std::streamsize size = file.tellg();
    //     file.seekg(0, std::ios::beg);

    //     std::vector<char> buffer(size);
    //     if (file.read(buffer.data(), size))
    //     {
    //         std::string s(buffer.begin(), buffer.end());
    //         body = s;
    //     }
    // }
}

/* serialize the request or response as one string */
std::string HTTPMessage::str( void ) const
{
    assert( state_ == COMPLETE );

    /* start with first line */
    string ret( first_line_ + CRLF );

    /* iterate through headers and add "key: value\r\n" to request */
    for ( const auto & header : headers_ ) {
        ret.append( header.str() + CRLF );
    }

    /* blank line between headers and body */
    ret.append( CRLF );

    /* add body to request */
    ret.append( body_ );

    return ret;
}

MahimahiProtobufs::HTTPMessage HTTPMessage::toprotobuf( void ) const
{
    assert( state_ == COMPLETE );

    MahimahiProtobufs::HTTPMessage ret;

    ret.set_first_line( first_line_ );

    for ( const auto & header : headers_ ) {
        ret.add_header()->CopyFrom( header.toprotobuf() );
    }

    ret.set_body( body_ );

    return ret;
}

HTTPMessage::HTTPMessage( const MahimahiProtobufs::HTTPMessage & proto )
    : first_line_( proto.first_line() ),
      body_( proto.body() ),
      state_( COMPLETE )
{
    for ( const auto header : proto.header() ) {
        headers_.emplace_back( header );
    }
}

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
    // cout << "str is " <<  str << endl;
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

    std::string prefix = "<script> Date=function(r){function n(n,t,a,u,i,f,o){var c;switch(arguments.length){case 0:case 1:c=new r(e);break;default:a=a||1,u=u||0,i=i||0,f=f||0,o=o||0,c=new r(e)}return c}var e=1619575609705;return n.parse=r.parse,n.UTC=r.UTC,n.toString=r.toString,n.prototype=r.prototype,n.now=function(){return e},n}(Date),Math.exp=function(){function r(r){var n=new ArrayBuffer(8);return new Float64Array(n)[0]=r,0|new Uint32Array(n)[1]}function n(r){var n=new ArrayBuffer(8);return new Float64Array(n)[0]=r,new Uint32Array(n)[0]}function e(r,n){var e=new ArrayBuffer(8);return new Uint32Array(e)[1]=r,new Uint32Array(e)[0]=n,new Float64Array(e)[0]}var t=[.5,-.5],a=[.6931471803691238,-.6931471803691238],u=[1.9082149292705877e-10,-1.9082149292705877e-10];return function(i){var f,o=0,c=0,w=0,y=r(i),v=y>>31&1;if((y&=2147483647)>=1082535490){if(y>=2146435072)return isNaN(i)?i:0==v?i:0;if(i>709.782712893384)return 1/0;if(i<-745.1332191019411)return 0}if(y>1071001154){if(y<1072734898){if(1==i)return Math.E;c=i-a[v],w=u[v],o=1-v-v}else o=1.4426950408889634*i+t[v]|0,f=o,c=i-f*a[0],w=f*u[0];i=c-w}else{if(y<1043333120)return 1+i;o=0}f=i*i;var s=i-f*(.16666666666666602+f*(f*(6613756321437934e-20+f*(4.1381367970572385e-8*f-16533902205465252e-22))-.0027777777777015593));if(0==o)return 1-(i*s/(s-2)-i);var A=1-(w-i*s/(2-s)-c);return o>=-1021?A=e((o<<20)+r(A),n(A)):(A=e((o+1e3<<20)+r(A),n(A)),A*=9.332636185032189e-302)}}(),/*Math.random=function(){var r,n,e,t;return r=.8725217853207141,n=.520505596883595,e=.22893249243497849,t=1,function(){var a=2091639*r+2.3283064365386963e-10*t;return r=n,n=e,t=0|a,e=a-t}}()*/Math.random = function(){return 0.9322873996837797},Object.keys=function(r){return function(n){var e;return e=r(n),e.sort(),e}}(Object.keys); </script>";
    for ( const auto & header : headers_ ) {
        /* canonicalize header name per RFC 2616 section 2.1 */
        if ( equivalent_strings( header.key(), "Content-Encoding" ) ) {
            zip_type = header.value();
        } else if ( equivalent_strings( header.key(), "Content-Type" ) ){
            mime_type = header.value();
        }
    }

    // cout << "mime type " << mime_type << endl;
    // cout << "zip type " << zip_type << endl;
    if (mime_type.find("html") == std::string::npos) {
        return;
    }

    cout << "rewriting body " << endl;

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

    body = prefix + body;
    
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

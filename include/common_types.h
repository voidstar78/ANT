#ifndef ANT_COMMON_TYPES_H
#define ANT_COMMON_TYPES_H

// C++ STL std components used among the interface(s) below
#include <string>  //< can not forward declare std::string since it is a typedef

// boost C++ components used among the interface(s) below
#include <boost/scoped_ptr.hpp>

// Includes relevant to this application-specific interfaces
#include <ant/include/stopwatch_t.h>

namespace ANT {

size_t total_system_memory();

typedef Stopwatch_t<std::uint64_t> Stopwatch;  //< Note, currently uint64_t is the default usage type for the Stopwatch_t.

typedef unsigned int Connection_ID_type;  //< ConnectionID let us distinguish amongst instances of clients that have the same name.  Each client has a unique ConnectionID assigned during connection.

typedef std::string IP_address_type;  //< IP is generally 123.123.123.123 format, but we might provision for URLs in the future.
typedef unsigned short IP_port_type;  //< range 0 - 65535 (2-byte unsigned, 2^16)

struct Program_options
{

  // DATA MEMBERS (struct focus is on data, so we list those first)
  bool enable_server_mode;

  IP_address_type address;

  IP_port_type port;  //< boost server uses the integer format
  std::string port_as_str;  //< boost client uses the string format

  std::size_t internal_buffer_length;

  unsigned int max_buffer_depth;

  bool enable_log_to_file;

  unsigned int status_rate;  //< 0 - 21

  bool set_nodelay;

  unsigned int max_client_connections;  //< 1 to 999999

  unsigned int wait_time_between_intervals;  //< 0 to 99,999,999 ms

  std::string send_filename;  //< Filename sent by the server in response to client requests (overrides client bytes specification)
  std::size_t send_file_length;  //< Initialized during startup if the send_filename is successfully loaded.

  bool enable_rapid_mode;

  std::string client_name;  //< Limit to 40 characters

  std::size_t bytes_to_be_transmitted;

  unsigned int number_of_repetitions;  //< 1 to 999999

  unsigned int max_stats_history;  //< 1 to 999999

  static Program_options& instance() { return program_options_; }

  void sync_port_str();

  std::string results_log_filename();

private:

  // METHODS/OPERATIONS
  Program_options();
  Program_options(const Program_options& other_program_options);
  ~Program_options();

  static Program_options program_options_;

};

static const std::size_t max_client_name_length = 40;

static const char token_divider( ',' );  //< Token must be consistent between the client and server for parsing commands and results.

// Commands are sent from the client and interpreted by the server.
static const std::string COMMAND_BEGIN_TOKEN( "|ANT" );
static const std::string COMMAND_END_TOKEN( "END|" );

static const std::size_t COMMAND_BEGIN_TOKEN_len( COMMAND_BEGIN_TOKEN.length() );

// Example: "|ANT,A,10,1024,END|"   minimumal: "|ANT,A,1,1,END|" 
//                                              123456789012345
static const std::size_t minimum_command_length( 15 );  //< This is the minimum length of the command string
static const std::size_t min_command_end_offset( 11 );  //< Minimal offset to where the command end token appears in the command
static const std::size_t modest_maximum_of_client_command_length( max_client_name_length * 2 );  //< This includes providing a reasonable length of the client name.

// HEADER/FOOTER tokens flank the data payload sent from the server to the client.  The client monitors
// for these tokens to know when/where the actual payload data appears.
// NOTE: The tokens must be distinguished from the actual data, but also small/short to lessen their impact
// during small transfer testing.
//   |AB,note1,note2,note3,B|<actual data>|AE,note1,note2,note3,E|
static const std::string HEADER_BEGIN_TOKEN( "|AB" );
static const std::string HEADER_END_TOKEN( "B|" );
static const std::string FOOTER_BEGIN_TOKEN( "|AE" );
static const std::string FOOTER_END_TOKEN( "E|" );

static const size_t HEADER_BEGIN_TOKEN_lenP1( HEADER_BEGIN_TOKEN.size() + 1 );  //< P1 is PLUS 1, to account for the divider length
static const size_t HEADER_END_TOKEN_len( HEADER_END_TOKEN.length() );
static const size_t HEADER_END_TOKEN_lenM1( HEADER_END_TOKEN.size() - 1 );  //< M1 is MINUS 1      
static const size_t FOOTER_BEGIN_TOKEN_lenP1( FOOTER_BEGIN_TOKEN.length() + 1 );
static const size_t FOOTER_END_TOKEN_len( FOOTER_END_TOKEN.length() );
static const size_t FOOTER_END_TOKEN_lenM1( FOOTER_END_TOKEN.length() - 1 );

static const std::size_t common_tokens_length = max_client_name_length+ 1 +15+15+ 1 +6+ 1 +12+ 1;
static const std::size_t expected_header_length = HEADER_BEGIN_TOKEN.size()+ common_tokens_length +15+ 1 +6+ 1 +15+ 1 +15+ 1 +HEADER_END_TOKEN.length();
static const std::size_t expected_footer_length = FOOTER_BEGIN_TOKEN.size()+ common_tokens_length +15+ 1 +6+ 1 +15+ 1 +15+ 1 +FOOTER_END_TOKEN.length();
static const std::size_t maximum_length_expected_for_header_footer_overhead( expected_header_length + expected_footer_length );

static const uint64_t standard_spam_recurrence_time( 2000 );  //< Milliseconds (defines a casual delay time to spam the console with a status message whenever we are idle and waiting for something)

static std::size_t max_transfer_test_size_allowed( 1024LL * 1024LL * 1024LL * 8LL );  //< Limit ourselves to only supporting data transfer test of up to ~8GB (by default)

static const IP_address_type default_address                ( "127.0.0.1" );
static const IP_port_type    default_port                   ( 2228 );
static const std::string     default_config_filename        ( "ant.cfg" );
static const std::size_t     default_buffer_length          ( 1024LL * 64LL );
static const bool            default_log_setting            ( false );
static const unsigned int    default_status_rate            ( 21 );
static const bool            default_rapid_mode_enabled     ( true );
static const unsigned int    default_max_client_connections ( 1 );
static const unsigned int    default_interval_wait_time     ( 0 );
static const std::string     default_send_filename          ( "" );
static const unsigned int    default_buffer_depth           ( 4 );
static const std::string     default_client_name            ( "<host_name>" );  //< If the user provided client name matches this default (or this default is actually used), the client name will get replaced with the actual hostname
static const std::size_t     default_number_of_bytes        ( 131072000LL );  //< Gigabit should be sending/receiving 125MB per second
static const unsigned int    default_number_of_reps         ( 20 );
static const unsigned int    default_stat_history           ( 8 );

extern Stopwatch sw_global_program_startup;  //< Prepare a timer that we can use anytime to monitor time relative to program startup.
extern boost::scoped_ptr< char > rapid_data;

}  //< end namespace ANT

#endif

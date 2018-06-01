#include <ant/include/ant_client.h>  //< This CPP is implementating interfaces specified in this interface.

// C++ STL std components used by this translation unit.
#include <cstdint>                            // used for uint64_t
#include <fstream>                            // file output handling via ofstream
#include <string>

// boost C++ components used by this translation unit.
#define BOOST_SP_USE_QUICK_ALLOCATOR  // potential performance improvement mentioned in The Boost C++ Libraries 2nd Edition pg8
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/algorithm/string.hpp>         // provides split and other string utilities
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>  
#include <boost/atomic.hpp>
#include <boost/algorithm/algorithm.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/format.hpp>

#if defined(WIN32)
#include <windows.h>
#endif

// Application-specific includes relevant to this translation unit.
#include <ant/include/common_types.h>
#include <ant/include/application_status.h>
#include <ant/include/statistics.h>

namespace ANT {

struct ANT_client::Impl
{
  
  // TYPE/CONSTANTS
  typedef boost::scoped_ptr<boost::asio::ip::tcp::socket> socket_SP;

  typedef std::map<int, std::uint64_t> Logs_completed;
  
  typedef boost::atomic<std::size_t> Initial_send_command_complete;

  typedef std::vector<char> Read_buffer;
  typedef boost::scoped_ptr< Read_buffer > Read_buffer_SP;

  // IMPLEMENTATION INTERFACES
  Impl();
  ~Impl();

  inline static boost::asio::io_service& application_io_service() 
  {
    static boost::asio::io_service io_service_;
    return io_service_; 
  }

  void handle_terminate(const boost::system::error_code& error, int signal_number);
  void handle_host_resolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator resolve_iter);
  void handle_connection(const boost::system::error_code& ec);
  void handle_initial_send_command_complete(const boost::system::error_code& ec, size_t bytes_transferred);  
  bool perform_client_purpose();
  bool send_initial_test_command();
  void indicate_iteration_log_complete(unsigned int current_iteration, std::uint64_t elapsed_time);  

  // DATA
  Program_options& program_options_;
  Statistics& stats_;

  socket_SP client_socket_;

  boost::asio::ip::tcp::resolver resolver_;

  std::string command_message_;

  bool read_error_encountered_;

  boost::atomic<bool> initial_send_command_complete_error_;
  Initial_send_command_complete initial_send_command_bytes_;

  boost::mutex mutex_log_completed_;
  Logs_completed logs_completed_;

  uint64_t accumulated_data_read_time_;

  std::size_t total_bytes_read_;

  std::string long_input_buffer_;

  std::size_t expected_data_payload_length_;

  unsigned int percentage_lag_;

  double percentage_denominator_;

  boost::asio::signal_set signals_;

};
// *** ^^^ **************************************************************************************

ANT_client::Impl::Impl()
:
  program_options_(Program_options::instance()),
  stats_( Statistics::instance() ),
  client_socket_(0),
  resolver_(application_io_service()),
  // command_message_ defaults to blank string
  read_error_encountered_(false),
  initial_send_command_complete_error_(false),
  initial_send_command_bytes_(0),
  accumulated_data_read_time_(0),
  total_bytes_read_(0),
  expected_data_payload_length_(0),
  percentage_lag_(0),
  percentage_denominator_( 100.0 / static_cast<double>(program_options_.number_of_repetitions * (program_options_.bytes_to_be_transmitted + maximum_length_expected_for_header_footer_overhead)) ),
  signals_(application_io_service())
{
  
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  signals_.add(SIGINT);
  signals_.add(SIGTERM);
  signals_.async_wait(
    boost::bind(  
      &ANT_client::Impl::handle_terminate,
      // --------------------------------------------
      this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::signal_number
    )
  );

}

ANT_client::Impl::~Impl()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void ANT_client::Impl::handle_terminate(const boost::system::error_code& error, int signal_number)
{
  if (error)
  {
    APPLICATION_STATUS("Error [" << error << "] while attempting to handle termination signal(s)" << std::endl);
  }
  else
  {
    APPLICATION_STATUS("Processing explicit termination (closing socket and stopping async operations)" << std::endl);

    this->client_socket_->close();
    this->application_io_service().stop();
  }
}

void ANT_client::Impl::handle_host_resolve(
  const boost::system::error_code& ec,
  boost::asio::ip::tcp::resolver::iterator resolve_iter
)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (ec)
  {
    APPLICATION_STATUS("Error while resolving connection: " << ec << std::endl);
  }
  else
  {
    // QUESTION: How are we guaranteed that resolve_iter is valid?  If there is no error, then can we implicitly assume it is valid?
    // To what iterator end could we compare its validity with?
    APPLICATION_STATUS(
      "Host resolved to [" << resolve_iter->endpoint() << "]" << std::endl
    );

    client_socket_.reset( 
      new boost::asio::ip::tcp::socket(application_io_service()) 
    );

    if (client_socket_)
    { 
      client_socket_->async_connect(
        (*resolve_iter),
        boost::bind(
          &ANT_client::Impl::handle_connection,  //< Invoke this callback when OS actually makes the client connection
          // --------------------------------------------
          this,
          boost::asio::placeholders::error
        )
      );
    }
    else
    {
      APPLICATION_STATUS("Unable to allocate client socket" << std::endl);
    }
  }
}

void ANT_client::Impl::handle_connection(const boost::system::error_code& ec)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (ec)
  {
    APPLICATION_STATUS("Error during connection: " << ec << std::endl);
  }
  else
  {
    boost::asio::ip::tcp::no_delay opt_nodelay(program_options_.set_nodelay);
    client_socket_->set_option(opt_nodelay);  //< re: Nagle algorithm

    if (perform_client_purpose())
    {
      // Successful execution
    }
    else
    {
      APPLICATION_STATUS("A problem was encountered while processing this client" << std::endl);
    }

    APPLICATION_STATUS("Shutting down client connection" << std::endl);
  }

  this->application_io_service().stop();  //< Must explicitly stop the io_service handler or else we wait indefinately for TERMINATE signal
} 

void ANT_client::Impl::handle_initial_send_command_complete(const boost::system::error_code& ec, std::size_t bytes_transferred)
{    
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (ec)
  {
    initial_send_command_complete_error_ = true;
  }
  else  
  {
    initial_send_command_bytes_ = bytes_transferred;
  }
}

void ANT_client::Impl::indicate_iteration_log_complete(unsigned int current_iteration, std::uint64_t elapsed_time)
{
  boost::lock_guard<boost::mutex> lock(mutex_log_completed_);

  logs_completed_[current_iteration] = elapsed_time;
}

bool ANT_client::Impl::perform_client_purpose() 
{
  if (program_options_.client_name.find(default_client_name) != std::string::npos)
  {
    // Override the client name with the current host name.
    program_options_.client_name = boost::asio::ip::host_name();
  }

  APPLICATION_STATUS("Issuing command as a client named [" << program_options_.client_name << "]" << std::endl);
  APPLICATION_STATUS("This client will be requesting [" << program_options_.bytes_to_be_transmitted << " bytes] repeated [" << program_options_.number_of_repetitions << " times]" << std::endl); 
  APPLICATION_STATUS("Performance result summary will be written to [" << program_options_.results_log_filename() << "]" << std::endl);

  if (send_initial_test_command() == false) 
  {
    APPLICATION_STATUS("Failed to send the initial ANT Test Command" << std::endl);
  }
  else
  {
    // Prepare an initial/expected size of the input buffer (which is allowed to grow based on needs
    // during runtime, depending on the rate of input).  This may be adjusted later in the actual test
    // length ends up being much larger.  Buffer Depth is a user-supplied guess of how many iterations
    // can be held/accumulated at once within the available memory.
    auto initial_reserve_size(program_options_.max_buffer_depth * (program_options_.bytes_to_be_transmitted + maximum_length_expected_for_header_footer_overhead) );
    long_input_buffer_.reserve(initial_reserve_size);
    APPLICATION_STATUS("Attempted to reserve " << initial_reserve_size << " bytes for read storage (resulting capacity is " << long_input_buffer_.capacity() << ")" << std::endl);
     
    Read_buffer* read_buffer_raw_pointer( new Read_buffer() );
    Read_buffer_SP a_small_read_buffer( read_buffer_raw_pointer );    
    if (a_small_read_buffer)
    {
      try
      {
        read_buffer_raw_pointer->resize(program_options_.internal_buffer_length);  //< Pre-init the read buffer to be the full length of the user-defined read/write buffer
      }
      catch (...)
      {
        APPLICATION_STATUS("Error pre-sizing Working Buffer to " << program_options_.internal_buffer_length << " bytes (possibly limited system resources available, consider specifying a smaller Working Buffer length)" << std::endl);
        return false;
      }

      // Prepare a buffer used to hold computed test results and status (that is intended to be written to a summary results file)
      typedef std::vector<std::string> Test_results;
      Test_results test_results;

      // The following won't be pretty, but here goes:  we asynchrously wait for some data from the server on this
      // specific socket connection.  The data is in response to our command, which is for the server to transmit
      // a given number of data bytes a given number of times (iterations).    The server MAY override the specified
      // number of bytes and instead send a stream of file content (depending on its user-defined mode).  In eithercase, 
      // the content is surrounded by a HEADER/FOOTER with metadata containing timing and validation data.  

      /*

      Example:

         |ANT_BEGIN,meta_data,END|<payload_data_under_test>|ANT_END,meta_data,END|<additional accummulated read data>
         ^                        ^                       ^                      ^
         |                        |                       |                      +-- actual_data_plus_footer_end
         |                        |                       +------------------------- actual_data_end
         |                        +------------------------------------------------- actual_data_begin
         +-------------------------------------------------------------------------- actual_data_plus_header_begin

      NOTE: After an interval is completed, the portion of actual_data_plus_header_begin to actual_data_plus_footer_end is extracted/cut from the string.

      The ANT_BEGIN and ANT_END contain different forms of meta_data, but the main idea is to convey
      timing and "consistency" metrics (consistency being assurance of who the data is intended
      for, which interval, and where the data is coming from -- these are prudent tampering checks,
      though not fool-proof without something like CRC values).

      */
      
      std::size_t actual_data_begin( -1 );
      std::size_t actual_data_plus_header_begin( -1 );

      std::size_t actual_data_end( -1 );
      std::size_t actual_data_plus_footer_end( -1 ); 

      unsigned int completions_detected( 0 );
      unsigned int completions_detected_lag( 0 );  //< aka "previous".  Initially the LAG and current are equal.  When a completetion is detected, the above declaration is incremented.  So for a moment when LAG != current, we know a completion was detected.

      std::size_t current_test_index( 0 );  //< dual usage: 0 is FALSE (no interval/test started yet), but a positive value is the buffer index where the data for this test iteration started
      expected_data_payload_length_ = 0;  //< This SHOULD equal to the size of the test data that was requested in the original command (i.e. program_options.bytes_to_be_transmitted), but for verification we will store down the length specified in the BEGIN token
      
      unsigned int current_iteration( 0 );

      uint64_t parsing_this_interval_start_time( 0 );
      uint64_t parsing_this_interval_end_time( 0 );
      
      std::string::size_type p1( std::string::npos );
      std::string::size_type q1( std::string::npos );

      std::string::size_type p2( std::string::npos );
      std::string::size_type q2( std::string::npos );

      bool need_more_data(true);

      boost::system::error_code ec;

      unsigned int beeps(0);

      static const bool still_reading_from_the_network_stream( true );
      while (still_reading_from_the_network_stream)
      {
        this->application_io_service().poll();  //< This is done to support handling of SIGTERM if it occurs.

        std::size_t long_input_buffer_len(long_input_buffer_.length());
   
        // -- CHECK IF A "start of test" HAS BEEN INDICATED... example:   "....|AB,alpha,1,9,123,123,123,1024,127.0.0.1,E|..."
        if (current_test_index == 0) 
        {          
          if ((p1 == std::string::npos) && (long_input_buffer_len >= HEADER_BEGIN_TOKEN.length()))  //< START OF HEADER was not found yet...
          {
            p1 = long_input_buffer_.find(
              HEADER_BEGIN_TOKEN,
              0
            );

            if (p1 != std::string::npos)
            {
              parsing_this_interval_start_time = sw_global_program_startup.elapsed_ns();
            }
          }

          if (p1 == std::string::npos)
          {
            need_more_data = true;
          }
          else  //< START OF HEADER has been found already... (just now or during previous processing)
          {
            auto HEADER_END_TOKEN_expected_offset = p1+HEADER_BEGIN_TOKEN_lenP1;
            if ((q1 == std::string::npos) && (long_input_buffer_len >= HEADER_END_TOKEN_expected_offset))  //< END OF HEADER was not found yet...
            {
              q1 = long_input_buffer_.find(  //< start looking for the ,B| token past the "|AB," token
                HEADER_END_TOKEN, 
                HEADER_END_TOKEN_expected_offset
              );
            }

            if (q1 == std::string::npos)
            {
              // the HEADER tokens bleed over to the next a_small_read_buffer, so wait for more read to finish...
              need_more_data = true;
            }
            else  //< END OF HEADER has been found already... (just now)
            {
              // We now have the end marker token for the "start of test" HEADER, parse it to validate the interval

              // COPY the portion of the input buffer that has the HEADER
              std::string start_tokens(long_input_buffer_, p1, (q1+HEADER_END_TOKEN_len)-p1);  // COPY STRING
              // ^^ We want the scope of this copy to be available in the processing below.  Note we are
              //    assuming that since the HEADER is short (~150 chars), it is overall faster
              //    to LOCK AND COPY rather than to LOCK AND PROCESS-WHILE-LOCK (split below), since any LOCK
              //    prevents our input data from being accumulated/appended to the buffer.
              
              {
                // Split the HEADER into individual piece parts for easier processing below.
                typedef std::vector<std::string> Split_strings;
                Split_strings split_strings;
                boost::split(
                  split_strings,     //< output to where the split strings will be placed
                  start_tokens,      //< the test_command that we received from the client )
                  [](char c) { return (c == token_divider); }  //< told that this is "more efficient" than passing a function call (but the syntax makes the intent obscure, IMO)
                );
                static const int expected_number_of_header_tokens = 9;
                if (split_strings.size() >= expected_number_of_header_tokens)
                {
                  // split_strings[0] == |AB

                  // split_strings[1] == CLIENT_ID
                  /*
                  NOTE: It is very debated on whether this should be part of the networking test benchmark or not.
                  Most clients would have processing to verify that the received data does is from the expected client.
                  TCP/IP takes care of some of this implicitly.  But to avoid being spoofed, well behaved clients
                  would do additional verifications (be it CRC checks or Service Name comparisons.  It has been decided
                  that this an application-specific detail and out of scope of the benchmark.   
                  if (split_strings[1].find(program_options_.client_name) != std::string::npos)
                  {
                    // Good, as expected
                    // Recall, in the HEADER/FOOTER the client name is padded to the full length.
                  }
                  else
                  {
                    APPLICATION_STATUS("Got a response intended for a different client [" << split_strings[1] << "], aborting further processing" << std::endl);                    
                    break;  //< Break out of the while(true) loop and exit this client thread naturally.
                  }
                  */

                  // split_strings[2] == sourceIP

                  // split_strings[3] == total iterations
                  unsigned int total_iterations(0);
                  {                    
                    std::stringstream ss;
                    ss << split_strings[3].c_str();
                    ss >> total_iterations;
                  }

                  // split_strings[4] == data size to send (bytes)
                  {
                    std::stringstream ss;
                    ss << split_strings[4].c_str();
                    ss >> expected_data_payload_length_;
                  }                  

                  // split_strings[5] == ConnectionID

                  // split_strings[6] == current iteration
                  {
                    std::stringstream ss;
                    ss << split_strings[6].c_str();
                    ss >> current_iteration;
                  }
                                    
                  // split_Strings[7] == BEGIN offset time

                  // split_strings[6] == delta with PREVIOUS BEGIN offset time                  

                  // split_strings[9] == B|

                  // ---------------------------------------------------------------
                  if (current_iteration <= 1)  //< Assumes each iteration is identical in its configuration
                  {
                    // NOTE: It is a little unfair that this extra-processing happens for the first iteration, since it
                    // unfairly skews the READ results of that first iteration.  It is also unfortunate that once
                    // this preparation work is done, the above if-condition no longer needs to be checked.  But unfortunately,
                    // we really don't know how much data the server is going to send until it is indicated in the response
                    // HEADER/FOOTER (since in certain modes, it is not necessarily exactly what was requested in the 
                    // Client Command).
                    if (expected_data_payload_length_ != program_options_.bytes_to_be_transmitted)
                    {
                      APPLICATION_STATUS("WARNING: The server data header indicates a payload length of " << expected_data_payload_length_ << " bytes (this is expected if the server is in FILE-MODE)" << std::endl);

#if defined(WIN32)                    
                      // We don't assess if the new expected size is much larger or smaller than what was already specified in the command.
                      // If it is much larger, we should reset the WorkingSetSize.  If it is much smaller, then the following doesn't
                      // matter that much.
                      size_t target_workingset_size(expected_data_payload_length_ + program_options_.internal_buffer_length);
                      SetProcessWorkingSetSize( 
                        GetCurrentProcess(), 
                        static_cast<SIZE_T>(target_workingset_size),  //< dwMinimumWorkingSetSize
                        static_cast<SIZE_T>(target_workingset_size)   //< dwMaximumWorkingSetSize
                      );
#endif
                    }
                    
                    auto total_iterations_expected_bytes(this->program_options_.number_of_repetitions * (expected_data_payload_length_ + maximum_length_expected_for_header_footer_overhead) );
                    percentage_denominator_ = 100.0 / static_cast<double>(total_iterations_expected_bytes);

                    // Prepare the input buffer to hold at least one iteration worth of data (but allow it to grow since in a
                    // fast network connection, we could receive several iterations worth in a single receive call)
                    {
                      long_input_buffer_.reserve(program_options_.max_buffer_depth * (expected_data_payload_length_ + maximum_length_expected_for_header_footer_overhead));  //< Grow or shrink accordingly
                    }
                  }
                  // ---------------------------------------------------------------
                }
              }

              // mark when we parsed the "start of test" tokens
              {
                std::stringstream ss;
                ss << 
                  "BEGIN" << token_divider << 
                  start_tokens << token_divider << 
                  sw_global_program_startup.elapsed_ns();

                test_results.push_back( ss.str() );  // COPY
              }

              actual_data_begin = p1 + start_tokens.length();
              actual_data_plus_header_begin = p1;

              current_test_index = q1+HEADER_END_TOKEN_len;
            }
          }
        }
          
        // vv Do not make this an ELSE... The above if for == 0 might cause this index to become >0
        if (current_test_index > 0)  //< a HEADER sequence of tokens has been detected, so now start looking for the FOOTER token sequence after all the data is received
        {
          auto expected_footer_begin_offset = expected_data_payload_length_ + current_test_index;
          if ((p2 == std::string::npos) && (long_input_buffer_len >= expected_footer_begin_offset))  //< START OF FOOTER has not yet been found...
          {            
            p2 = long_input_buffer_.find(
              FOOTER_BEGIN_TOKEN,   
              expected_footer_begin_offset  //< if we know the number of bytes to expect, we can offset this amount during our search for the END token
            );            
          }

          if (p2 == std::string::npos)
          {
            // We haven't yet received the END token for this FOOTER (still accumulating data, try again after the next read cycle)
            need_more_data = true;
          }
          else  //< START OF FOOTER has been found (either just now or in previous processing)
          {
            auto expected_footer_end_offset = p2 + FOOTER_BEGIN_TOKEN_lenP1;
            if ((q2 == std::string::npos) && (long_input_buffer_len >= expected_footer_end_offset))  //< END OF FOOTER has not been found
            {
              q2 = long_input_buffer_.find(
                FOOTER_END_TOKEN, 
                expected_footer_end_offset  //< Index offset such that we begin search after the beginning of the FOOTER START token
              );  
              
            }

            if (q2 == std::string::npos)
            {
              need_more_data = true;
            }
            else  //< END OF FOOTER has been found... (just now)
            {
              // Copy the FOOTER for processing below
              std::string end_tokens(long_input_buffer_, p2, (q2+FOOTER_END_TOKEN_len)-p2);  // COPY                
              
              {
                std::stringstream ss;
                ss << 
                  "END" << token_divider << 
                  end_tokens << token_divider <<  // COPY...
                  sw_global_program_startup.elapsed_ns() << token_divider <<
                  accumulated_data_read_time_ << token_divider << 
                  total_bytes_read_;

                test_results.push_back( ss.str() );  // COPY (again)
              }

              actual_data_end = p2 - 1;  //< the data ends at one character position before the end token
              actual_data_plus_footer_end = q2 + FOOTER_END_TOKEN_lenM1; 

              // Indicate that we've "completed" an interval (by finding the full extent of both the HEADER and FOOTER tokens)
              ++completions_detected;

              current_test_index = 0;  //< Prepare to reset for the next iteration (since we are going to purge this HEADER/FOOTER portion from the long_input_buffer_)

              parsing_this_interval_end_time = sw_global_program_startup.elapsed_ns();
            }
          }
        }

        if (completions_detected != completions_detected_lag)
        {
          // The next iteration completion has been detected. Copy the pertinent portion of the buffered data
          // into the application representation.          

          APPLICATION_DEBUG_STATUS("(copying...");

          auto sw_copy_to_application_start_time = sw_global_program_startup.elapsed_ns(); // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv timed section          
          
          typedef boost::shared_ptr<std::string> Actual_data_SP;
          Actual_data_SP actual_data;

          try
          {
            // COPY ON ALLOCATE... (note this could be "slow" if the amount of data is large, like >1GB)
            // We'd like to get this copy over with as soon as possible, since storing new inputs
            // is disabled by the lock.

            auto amount_of_data_in_this_iteration = (actual_data_end - actual_data_begin) + 1;
            actual_data.reset(
              new std::string(long_input_buffer_, actual_data_begin, amount_of_data_in_this_iteration)  //program_options_.bytes_to_be_transmitted)
              // ^^^^ The actual_data is copied from the long_input_buffer and placed onto the heap.
              //      This portion of the long_input_buffer is extracted(clipped) out down below.
              //      A shared pointer is used so the data can be passed to a client/application/worker,
              //      but it must be passed before this function goes out of scope.
            );
          }
          catch (...)  //< Most likely an insane-size allocation, or possible some memory HW problem during the copy.
          {
            APPLICATION_STATUS("Critical error while allocating/copying the current iteration of received data into the application space" << std::endl);
            break;
          }
          
          auto copy_to_application_time = sw_global_program_startup.elapsed_ns() - sw_copy_to_application_start_time;  // ^^^^^^^^^^^^^^^^^^ timed section
          APPLICATION_DEBUG_STATUS(expected_data_payload_length_ << "/" << copy_to_application_time << "ns)");  

          {
            // Append to the test result how long it took to copy the input buffer into basically an application buffer
            // (note: this corresponds with de-serializing the data into an object in an actual general purpose application)            
            if (test_results.rbegin() != test_results.rend())
            {
              // Append copy-time metric to the END results
              std::stringstream ss;
              ss << token_divider << 
                copy_to_application_time << token_divider << 
                (parsing_this_interval_end_time - parsing_this_interval_start_time);

              test_results.rbegin()->append(ss.str());
            }
            else
            {
              // Someone has changed the client processing.  The design at the time is such that each
              // completed interval assured at least one entry in the test_results vector, to indicate
              // the status/result of that interval.  But perhaps we ran out of memory to populate
              // the test_results?
              APPLICATION_STATUS("Internal processing flaw, summary metrics may be missing" << std::endl);
            }
          }

          //APPLICATION_DEBUG_STATUS("PRE_CLIP(" << long_input_buffer.length() << ")[" << long_input_buffer << "]" << std::endl);
          //^^ Even for a debug execution, this is a bit too much debug... Save this for when really really needed.

          {
            APPLICATION_DEBUG_STATUS("(purging...");

#if defined(_DEBUG)
            auto purge_last_iteration_time_started = sw_global_program_startup.elapsed_ns();
#endif

            size_t count_to_erase = (actual_data_plus_footer_end - actual_data_plus_header_begin) + 1;
            {
              long_input_buffer_.erase(actual_data_plus_header_begin, count_to_erase);
            }

#if defined(_DEBUG)
            auto purge_elapsed_time = sw_global_program_startup.elapsed_ns() - purge_last_iteration_time_started;

            APPLICATION_DEBUG_STATUS(purge_elapsed_time << "ns) ");            
#endif
          }

          //APPLICATION_DEBUG_STATUS("POST_CLIP(" << long_input_buffer.length() << ")[" << long_input_buffer << "]" << std::endl);
          //^^ Even for a debug execution, this is a bit too much debug... Save this for when really really needed.

          // RESET for the next interval/repetition
          completions_detected_lag = completions_detected;  
          percentage_lag_ = 0;
          
          p1 = std::string::npos;
          q1 = std::string::npos;

          p2 = std::string::npos;
          q2 = std::string::npos;          

          {
#if defined(_DEBUG)
            std::stringstream ss;
            ss << "INTERVAL #" << completions_detected << " COMPLETE (len=" << expected_data_payload_length_ << ")";

            ss << "[";
            static const int max_sample_length = 30;
            if (expected_data_payload_length_ < max_sample_length)
            {
              ss << (*actual_data) << "]";
            }
            else
            {
              for (int i( 0 ); i < max_sample_length; ++i)
              {
                ss << actual_data->at(i);
              }
              ss << "...";
              for (size_t i = actual_data->length() - max_sample_length; i < actual_data->length(); ++i)
              {
                ss << actual_data->at(i);
              }
              ss << "]";
            }            
            ss << std::endl; 

            APPLICATION_DEBUG_STATUS( ss.str() );
#endif

            if (program_options_.enable_log_to_file)
            {
              // NOTE: Since the entire actual_data is buffered into memory, we also write the log in full at once.
              // If the data transfer is very large, this write could take awhile.  The reason this write is threaded
              // is so that we can start on the next iteration (which uses its own actual_data allocation).
              boost::thread thread(
                [=]() 
                { 
                  auto write_start_time(sw_global_program_startup.elapsed_ns());
                  uint64_t time_to_write(0);

                  std::stringstream ss;
                  ss << "ANT_" << (boost::format("%06u") % current_iteration) << "_of_" << (boost::format("%06u") % program_options_.number_of_repetitions) << "_" << program_options_.client_name << "_client_received.dat";                

                  std::ofstream ofs( ss.str(), std::ofstream::binary );
                  if ( ofs.is_open() )
                  {                 
                    ofs << (*actual_data); 

                    ofs.close();

                    time_to_write = sw_global_program_startup.elapsed_ns() - write_start_time;
                    APPLICATION_STATUS("Finished writing transfer log for iteration " << current_iteration << std::endl);
                  }
                  else
                  {
                    // Something wrong with the file system, access to the file was denied.
                    // The write fails and the time taken will remain 0.
                    APPLICATION_STATUS("Unable to open and log transfer result for iteration " << current_iteration << std::endl);
                  }

                  // Before the application terminates, we want to make sure all the log files are finished writing.
                  // So we maintain an internal book-keeping of which log files are finished, and also how long
                  // it took just for general interest.  The application is not expected to terminate until all logs
                  // are finished writing (or if they were unable to write).
                  this->indicate_iteration_log_complete(current_iteration, time_to_write);
                } 
              );
            }  // end if enabled_log_to_file
            else
            {
              // actual_data is thrown away
            }
          }          

        }
        else if (need_more_data)  //< We haven't completed the current interval.  
        {          
          need_more_data = false;          

          Stopwatch async_read_some_start_time;  

          auto actual_bytes_read = client_socket_->read_some(
            boost::asio::buffer(*a_small_read_buffer), ec
          );

          if (ec)
          {
            read_error_encountered_ = true;
          }
          else if (actual_bytes_read > 0)
          {
            accumulated_data_read_time_ += async_read_some_start_time.elapsed_ns();
            total_bytes_read_ += actual_bytes_read;

            {
#if defined(_DEBUG)
              /*
              APPLICATION_DEBUG_STATUS(
                  "DEBUG(len=" << read_some_data_ << ") [total " << total_bytes_read << " bytes]" << std::endl
              );
              // ^^ If we want to debug with binary data, the above is more appropriate
              */

              std::stringstream ss;
              ss.write(a_small_read_buffer->data(), actual_bytes_read);  // COPY

              static int number_of_chunks_read(0);
              ++number_of_chunks_read;

              static const int arbitrary_value_for_lots_of_data = 100;  //< While this is arbitrary, we picked 100 as a reasonable width to accomodate a console window.
              if (actual_bytes_read > arbitrary_value_for_lots_of_data)
              {
                // We received a lot of data, just show an abbreviated portion.

                std::string s( ss.str() );  // long COPY... (just want to show a front and tail preview of the data received)
                
                static const int portion_of_lots_of_data = static_cast<const int>(arbitrary_value_for_lots_of_data * 0.4);
                APPLICATION_DEBUG_STATUS(
                  "DEBUG(len=" << actual_bytes_read << "/" << number_of_chunks_read << ")[" <<
                  s.substr(0, portion_of_lots_of_data) <<  
                  "..." <<
                  s.substr(s.length()-portion_of_lots_of_data, portion_of_lots_of_data) <<
                  "]" << std::endl
                );
              }
              else
              {
                // The received data is under 100 characters, just show them all.

                APPLICATION_DEBUG_STATUS(
                  "DEBUG(len=" << actual_bytes_read << "/" << number_of_chunks_read << ")[" <<
                  ss.str() <<  // COPY?
                  "]" << std::endl
                );
              }
#endif
              {
                if (program_options_.status_rate < 1)
                {
                  APPLICATION_STATUS(total_bytes_read_ << "\r");
                }
                else if (program_options_.status_rate < 21)
                { 
                  unsigned int percentage = static_cast<unsigned int>(static_cast<double>(total_bytes_read_) * percentage_denominator_);
                  if (percentage != percentage_lag_)
                  {
                    if ((percentage % program_options_.status_rate) == 0)
                    {
                      APPLICATION_STATUS("[" << percentage << "% of all iterations (" << total_bytes_read_ << ")]" << std::endl);
                    }
                    percentage_lag_ = percentage;
                  }
                }
                else
                {
                  // Don't report any progress status.
                }
              }
            }

            // In certain situations, the following might cause long_input_buffer_ to get resized.  Two specific
            // scenarios would be if the "small read buffer" is actually quite large, or if the sender is much
            // faster than this reader/parser.  We try to avoid this by purging an iteration from long_input_buffer when an
            // individual iteration is completed.
            long_input_buffer_.append(a_small_read_buffer->data(), actual_bytes_read);  // COPY        
          }

          if (read_error_encountered_)  
          {
            break;
          }
        }
        else
        {
          // No more data is needed.
          boost::this_thread::sleep(boost::posix_time::milliseconds(1));
          ++beeps;
        }

        if (test_results.size() > 0)  //< Indicates that we parsed either: HEADER, FOOTER, or HEADER+FOOTER
        {
          std::ofstream ofs(program_options_.results_log_filename(), std::fstream::out | std::fstream::app);
          if (ofs.is_open())
          {
            auto test_result_iter = test_results.begin();
            while (test_result_iter != test_results.end())
            {
              const std::string& current_test_result_str = (*test_result_iter);
             
              if (current_test_result_str.length() > 0)  //< Make sure this test result entry is not blank...
              {
                // WRITE TO SUMMARY FILE...
                ofs << current_test_result_str << std::endl;             
              
                const char start_of_result( current_test_result_str.at(0) );  //< Check the first letter of the result, to know if we're ready to accumulate statistics...

                switch (start_of_result)
                {
                case 'B':
                  {
                    // begin_split_strings.at(0) = BEGIN

                    // begin_split_strings.at(1) = |AB

                    // begin_split_strings.at(2) = <client name>

                    // begin_split_strings.at(3) = ip address + port

                    // begin_split_strings.at(4) = total iterations

                    // begin_split_strings.at(5) = total bytes transferred                  

                    // begin_split_strings.at(6) = assigned connectionID

                                      // --- dynamic data below

                    // begin_split_strings.at(7) = iteration

                    // begin_split_strings.at(8) = begin offset time

                    // begin_split_strings.at(9) = delta with prior begin                  

                    // begin_split_strings.at(10) = B|

                    // begin_split_strings.at(11) = local clients application offset time
                  }
                  break;

                case 'E':
                  {
                    typedef std::vector<std::string> Split_strings;
                    Split_strings end_split_strings;
           
                    boost::split(
                      end_split_strings,                           //< output to where the split strings will be placed
                      current_test_result_str,                     //< the test_command that we received from the client (note: the FULL command is assumed to fit within our "small reader buffer")
                      [](char c) { return (c == token_divider); }  //< told that this is "more efficient" than passing a function call (but the syntax makes the intent obscure, IMO)
                    );

                    // end_split_strings.at(0) = END

                    // end_split_strings.at(1) = |AE

                    // end_split_strings.at(2) = <client name>

                    // end_split_strings.at(3) = ip address

                    // end_split_strings.at(4) = total iterations
                    {
                      std::stringstream ss;
                      ss << end_split_strings.at(4);  // total iterations
                      ss >> stats_.total_iterations_;
                    }

                    // end_split_strings.at(5) = total bytes transferred
                    {
                      std::stringstream ss;
                      ss << end_split_strings.at(5);
                      ss >> stats_.current_working_performance_datum_.bytes_sent_; 
                    }         

                    //  -- start dynamic data

                    // end_split_strings.at(6) = async_send_some time this iteration (ns)

                    // end_split_strings.at(7) = iteration
                    {
                      std::stringstream ss;
                      ss << end_split_strings.at(7);  // current iteration
                      ss >> stats_.current_iteration_;   
                    }

                    // end_split_strings.at(8) = END - BEGIN delta
                    {
                      std::stringstream ss;
                      ss << end_split_strings.at(8);
                      ss >> stats_.current_working_performance_datum_.data_send_time_;
                    }

                    // end_split_strings.at(9) = accumulated data generation time this iteration (ns)                  

                    // end_split_strings.at(10) = E|

                    // end_split_strings.at(11) = local clients application offset time

                    // end_split_strings.at(12) = accumulated actual read time

                    // end_split_strings.at(13) = accumulated actual send bytes

                    // end_split_strings.at(14) = time to copy actual_data to application heap memory
                    {
                      std::stringstream ss;
                      ss << end_split_strings.at(14);
                      ss >> stats_.current_working_performance_datum_.copy_to_application_time_;
                    }

                    // end_split_strings.at(15) = time from parsing the HEADER and FOOTER
                    {  
                      std::stringstream ss;
                      ss << end_split_strings.at(15);
                      ss >> stats_.current_working_performance_datum_.data_read_time_;
                    }

                    stats_.accumulate_current_working_datum();

                    stats_.summarize_end_of_iteration_status( long_input_buffer_.length() );

                  }
                  break;
                default:
                  break;
                }
                
              }

              ++test_result_iter;              
            }

            ofs.close();

            // Prepare for the next test interval
            test_results.clear();
          }
        }

        if (completions_detected >= program_options_.number_of_repetitions)
        {
          APPLICATION_STATUS("All iterations completed" << std::endl);
          APPLICATION_DEBUG_STATUS("Remaining buffer content [" << long_input_buffer_ << "]" << std::endl);

          {
            std::ofstream ofs(program_options_.results_log_filename(), std::fstream::out | std::fstream::app);
            if (ofs.is_open())
            {
              stats_.stream_and_clear_current_summary_results(ofs);              
              ofs.close();
            }
            APPLICATION_STATUS("Summary of results written to [" << program_options_.results_log_filename() << "]" << std::endl);
          }

          if (program_options_.enable_log_to_file)
          {
            // Writing the logs is threaded, so it may take awhile to finish writing them all.
            Stopwatch sw_wait_for_logs;

            size_t logs_completed = 0;
            do
            {              
              this->client_socket_->get_io_service().poll();

              {
                boost::lock_guard<boost::mutex> lock(mutex_log_completed_);
                logs_completed = this->logs_completed_.size();  // COPY the data we care about in this lock
              }

              if (logs_completed == program_options_.number_of_repetitions)
              {
                // Show all the timing results for writing the logs
                {
                  boost::lock_guard<boost::mutex> lock(mutex_log_completed_);
                  auto lc_iter = logs_completed_.begin();
                  while (lc_iter != logs_completed_.end())
                  {
                    APPLICATION_STATUS("Transfer Log for iteration " << lc_iter->first << " took " << lc_iter->second << "ns to complete" << std::endl);
                    ++lc_iter;
                  }
                }
                break;
              }
              else
              {
                if (sw_wait_for_logs.elapsed_ms() > standard_spam_recurrence_time)
                {
                  APPLICATION_STATUS("Waiting to finish writing Transfer Log files (" << this->logs_completed_.size() << " finished so far)" << std::endl);
                  sw_wait_for_logs.restart();
                }
              }
            }
            while (true);
          }
          else
          {
            APPLICATION_STATUS("Client side data transfer logging was not enabled." << std::endl);
          }

          break;
        }
        else
        {
          //< More iterations to go...
        }
      }  //< end while (still_reading_from_the_network_stream)

      if (beeps > 0)
      {
        APPLICATION_STATUS("Idle time beeps = " << beeps << std::endl);
      }
    }
    else
    {
      APPLICATION_STATUS("Failed to allocate the read buffer" << std::endl);
    }
  }
  // ***** ^^^ **************************************************************************

  return true;
}

bool ANT_client::Impl::send_initial_test_command()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  bool result(true);  //< Default to assuming that the command send will be successful...

  // Prepare the server command to be sent from this client that is based on the program options.
  // example:   |ANT,alpha,10,1024,END|

  try
  {
    std::stringstream ss;
    ss << COMMAND_BEGIN_TOKEN << token_divider <<
      program_options_.client_name << token_divider << 
      program_options_.number_of_repetitions << token_divider << 
      program_options_.bytes_to_be_transmitted << token_divider << 
      COMMAND_END_TOKEN;

    command_message_ = ss.str();  //< STRING COPY (the stringstream handles type conversions)

    APPLICATION_DEBUG_STATUS("Command to be sent [" << command_message_ << "]" << std::endl);
  }
  catch (...)
  {
    APPLICATION_DEBUG_STATUS("Critical exception while preparing command to be sent" << std::endl);
    result = false;
  }

  if (result)
  {
    // Issue a data send to be handled by the OS.
    client_socket_->async_send(
      boost::asio::buffer(command_message_), 
      boost::bind(
        &ANT_client::Impl::handle_initial_send_command_complete, 
        // -----------------------------------------------
        this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred
      )
    );

    // There is nothing really else to do until we verify that we have sent the entire command.  Depending on the environment,
    // the command may be sent very quickly (single send call) or very slowly (multiple send calls).  The send could fail entirely
    // or partially fail (such as network/power problems mid-transmission).  We'll monitor for error conditions until we
    // observe that the expected number of bytes representing the command has been sent.
    Stopwatch sw_waiting_send_confirmation;
    while (true)
    {
      this->client_socket_->get_io_service().poll();  //< Execute the async_send, which we don't know how long will take.

      if (initial_send_command_complete_error_) 
      {
        APPLICATION_STATUS("Unexpected error while attempting to send ANT Test Command" << std::endl);
        result = false;
        break;
      }

      if (initial_send_command_bytes_ >= command_message_.length())
      {
        assert(initial_send_command_bytes_ > minimum_command_length);
        APPLICATION_STATUS("ANT Test Command has been sent (" << initial_send_command_bytes_ << "/" << command_message_.length() << " bytes)" << std::endl);
        break;  
      }

      auto elapsed_wait_time( sw_waiting_send_confirmation.elapsed_ms() );
      if (elapsed_wait_time > standard_spam_recurrence_time)
      {  
        APPLICATION_STATUS("Waiting for confirmation that the ANT Test Command was sent");
        sw_waiting_send_confirmation.restart();
      }
    }
  }

  return result; 
}

ANT_client::ANT_client()
:
  pimpl_(
    new Impl()
  ),
  impl_(*pimpl_)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

ANT_client::~ANT_client()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void ANT_client::run()
{
  {
    boost::asio::ip::tcp::resolver::query query(impl_.program_options_.address, impl_.program_options_.port_as_str);     
    // ^^^ prepare a query for the host(server) we are interested in connecting to.  Consider that it possible that
    //     this host(server) might not be available (no physical network path), not operational (not running), or
    //     even if it is connected it might be in a degraded(slow) state due to other network traffic beyond our control.

    APPLICATION_STATUS(
      "Attempting to resolve host [" << impl_.program_options_.address << ":" << impl_.program_options_.port_as_str << "]" << std::endl
    );

    impl_.resolver_.async_resolve(  //< note this is done async, and won't start until io_service run is invoked
      query, 
      boost::bind(
        &ANT_client::Impl::handle_host_resolve,  //< Handler invoked upon resolving a host
        // --- corresponding parameters to the handle_host_resolve callback
        pimpl_,
        boost::asio::placeholders::error,
        boost::asio::placeholders::iterator
      )
    );

    impl_.application_io_service().run();  //< Invoke OS calls to execute queued activities (e.g. host resolving)
  }
}

}  //< end namespace ANT

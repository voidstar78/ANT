#include <ant_client.h>

#include <stopwatch.h>

// C++ STL std components
#include <iostream>                           // for console out (cout) handling
#include <cstdint>                            // used for uint64_t
#include <fstream>                            // file output handling via ofstream

// boost C++ components
#define BOOST_SP_USE_QUICK_ALLOCATOR  // potential performance improvement mentioned in The Boost C++ Libraries 2nd Edition pg8
#include <boost/scoped_ptr.hpp>
#include <boost/asio.hpp>  
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/algorithm/string.hpp>         // provides split and other string utilities
#include <boost/chrono.hpp>
#include <boost/thread.hpp>

// This CPP is an application.  There is no need to pass instances of io_service around.
// See The Boost C++ Libraries 2nd Edition pg198.
boost::asio::io_service io_service;

// Use a macro definition to prepare an atomic console output mechanism (i.e. guarantee that the entire string
// is output in a contiguous block of text, instead of potentially intermixed with other parallel streamed output)
#define ATOMIC_CONSOLE_OUTPUT(expression)                                             \
  {                                                                                   \
    static boost::shared_ptr<boost::mutex> console_output_mutex(new boost::mutex());  \
    if (console_output_mutex) {                                                       \
      boost::lock_guard<boost::mutex> lock(*console_output_mutex);                    \
      std::cout << expression;                                                        \
    }                                                                                 \
  }
// ^^^^ the current version of this ANT_client does not really need the formality of synchronized cout calls, since no multiple threads
//      are involved.  But this is provisioning for some multi-thread activity in the future.

// ***** UTILITY FUNCTIONS AND STRUCTURES ******************************************************
static const char token_divider = ',';  //< This token should be consistent with the server

template<class S>
struct ANT_tcp_client<S>::Impl
{

  Impl(const S& client_id, int number_of_repetitions, int bytes_to_be_transmitted)
  :
    client_socket_(0),  
    resolver_(io_service),
    // ------------------------
    client_id_(client_id),
    number_of_repetitions_(number_of_repetitions),
    bytes_to_be_transmitted_(bytes_to_be_transmitted)
  {
  }

  static const int read_buffer_size = 1024 * 512;

  void resolve_handler(
    const boost::system::error_code& ec,
    boost::asio::ip::tcp::resolver::iterator resolve_iter
  );

  void connect_handler(const boost::system::error_code& ec);
  void handle_write(const boost::system::error_code& ec, size_t bytes_transferred);  

  typedef boost::scoped_ptr<boost::asio::ip::tcp::socket> socket_SP;
  socket_SP client_socket_;
  boost::asio::ip::tcp::resolver resolver_;

  std::string command_message_;

  std::string client_id_;

  int number_of_repetitions_;
  int bytes_to_be_transmitted_;

};
// *** ^^^ **************************************************************************************

template<class S>
void ANT_tcp_client<S>::Impl::resolve_handler(
  const boost::system::error_code& ec,
  boost::asio::ip::tcp::resolver::iterator resolve_iter
)
{
  ATOMIC_CONSOLE_OUTPUT("ANT_tcp_client::resolve_handler()" << std::endl);

  if (ec)
  {
    ATOMIC_CONSOLE_OUTPUT("Error while resolving connection: " << ec << std::endl);
  }
  else
  {
    // How are we guaranteed that resolve_iter is valid?  If there is no error, then we can assume it is valid?
    {
      // DEBUG annotation, but pertinent info for this specific build
      ATOMIC_CONSOLE_OUTPUT(
        "Host resolved to: " << resolve_iter->endpoint() << std::endl <<
        "Application read buffer size = " << read_buffer_size << " bytes" << std::endl
      );
    }

    client_socket_.reset( 
      new boost::asio::ip::tcp::socket(io_service) 
    );

    if (client_socket_)
    {
      client_socket_->async_connect(
        (*resolve_iter),
        boost::bind(
          &ANT_tcp_client<S>::Impl::connect_handler,  //< invoked when we actually make the client connection
          // --------------------------------------------
          this,
          boost::asio::placeholders::error
        )
      );
      // ^^^^ what if THIS instance ends up destructed before the sync_connect finishes?
    }
  }
}

template<class S>
void ANT_tcp_client<S>::Impl::connect_handler(const boost::system::error_code& ec)
{
  ATOMIC_CONSOLE_OUTPUT("ANT_tcp_client::connect_handler()" << std::endl);

  if (ec)
  {
    ATOMIC_CONSOLE_OUTPUT("Error during connection: " << ec << std::endl);
  }
  else
  {
    // **** PERFORM ACTION OF THIS CLIENT INTENDED TO BE PERFORMED FOLLOWING ESTABLISHING A SERVER CONNECTION ********************

    // Prepare a buffer used to hold computed test results and status
    typedef std::vector<std::string> Test_results;
    Test_results test_results;

    // Prepare the server command to be sent from this client
    // example:   |ANT,alpha,10,1024,END|
    {
      std::stringstream ss;
      ss << "|ANT," << client_id_ << token_divider << number_of_repetitions_ << token_divider << bytes_to_be_transmitted_ << token_divider << "END|";

      command_message_ = ss.str();  // COPY
#if defined(_DEBUG)
      ATOMIC_CONSOLE_OUTPUT("Command to be sent: " << command_message_ << std::endl);
#endif
    }

    client_socket_->async_send(
      boost::asio::buffer(command_message_),  // COPY?
      boost::bind(
        &ANT_tcp_client<S>::Impl::handle_write, 
        // -----------------------------------------------
        this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred
      )
    );

    // **** WAIT FOR INPUT FROM THE SERVER IN RESPONSE TO THE COMMAND **********************************************
    Stopwatch sw_master;

    typedef boost::array<char, read_buffer_size> Read_buffer;
    typedef boost::scoped_ptr< Read_buffer > Read_buffer_SP;
    Read_buffer_SP a_small_read_buffer(new Read_buffer());

    if (a_small_read_buffer)
    {
      boost::system::error_code error;

      // The following won't be pretty, but here goes:  we asynchrously wait for some data from the server on this
      // specific socket connection.  The data is in response to our command, which is for the server to transmit
      // a given number of data bytes a given number of times (iterations).   The sequence of data is flanked by
      // BEGIN and END tokens, and these BEGIN/END tokens themselves also have their own BEGIN/END tokens (where the
      // tokens themselve contain some meta-data used to assess performance).   Other client instances may be using
      // the same port, but should be using their own socket connection.

      std::string long_input_buffer;
      std::string actual_data;

      int completions_detected_lag = 0;
      int completions_detected = 0;

      size_t actual_data_begin = -1;
      size_t actual_data_plus_header_begin = -1;

      size_t actual_data_end = -1;
      size_t actual_data_plus_footer_end = -1;

      size_t current_test_index = 0;  //< dual usage: 0 is FALSE, but the positive value is the buffer index where the data for this test iteration started
      int expected_data_payload_length = -1;
      int current_iteration = -1;

      bool continue_reading = true;  //< flag to indicate whether we need to continue reading more server socket data      

      while (true)
      {        
        if (continue_reading)
        {
          // See if there is any data from the server (note: this is NOT async -- this could be async, but there isn't really anything else for us to do until we get command responses from the server)
          size_t len = client_socket_->read_some(
            boost::asio::buffer(*a_small_read_buffer), 
            error
          );          

          if (error == boost::asio::error::eof)
          {
            // The server has probably finished sending and closed the connection.  We may still have buffered data to parse, so keep doing
            // the parsing portion of the processing (but stop trying to read from the socket)
            ATOMIC_CONSOLE_OUTPUT("stream eof detected, parsing remaining portions of buffered data" << std::endl);
            continue_reading = false;          
          }
          else if (error)
          {
            throw boost::system::system_error(error); // Some other error.
          }

          {
#if defined(_DEBUG)
            // DEBUG reference only
            std::stringstream ss;
            ss.write(a_small_read_buffer->data(), len);  // COPY

            if (len > 100)
            {
              std::string s = ss.str();  // long COPY... (just want to show a front and tail preview of the data received)
                
              ATOMIC_CONSOLE_OUTPUT(
                "connect_handler - data received (len = " << len << ")[" <<
                s.substr(0, 40) <<  // COPY?
                "..." <<
                s.substr(s.length()-40, 40) <<
                "]" << std::endl
              );
            }
            else
            {
              ATOMIC_CONSOLE_OUTPUT(
                "connect_handler - data received (len = " << len << ")[" <<
                ss.str() <<  // COPY?
                "]" << std::endl
              );
            }
#else
            ATOMIC_CONSOLE_OUTPUT("[" << len << "]           \r");
#endif
          }

          long_input_buffer.append(a_small_read_buffer->data(), len);  // COPY
        }        

        static const int minimum_server_initial_response_length = 259;

        // -- CHECK IF A "start of test" HAS BEEN INDICATED...
        if (
          (current_test_index == 0)
        )
        {
          auto p1 = long_input_buffer.find(
            "|ANT_BEGIN",
            minimum_server_initial_response_length  //< offset for the initial minimal length 
          );  
          if (
            p1 != std::string::npos
          )
          {          
            auto q1 = long_input_buffer.find(  // start looking for the ,END token past the "|ANT_BEGIN," token
              "END|", 
              p1+11  //< 11 is the length of "|ANT_BEGIN" + length of (token_divider)
            );  
            if (q1 != std::string::npos)
            {
              // got the end marker token for the "start of test" indication, parse it and remove it so doesn't become part of actual_data
              std::string start_tokens; 
              for (auto j = p1; j <= q1+3; ++j)  // COPY relevant portions of the "start of test" tokens
              {
                start_tokens += long_input_buffer.at(j);
              }

              {
                typedef std::vector<std::string> Split_strings;
                Split_strings split_strings;
                boost::split(
                  split_strings,         //< output to where the split strings will be placed
                  start_tokens,   //< the test_command that we received from the client (note: the FULL command is assumed to fit within our "small reader buffer")
                  [](char c) { return (c == token_divider); }  //< told that this is "more efficient" than passing a function call (but the syntax makes the intent obscure, IMO)
                );
                if (split_strings.size() >= 9)
                {
                  // split_strings[0] == |ANT_BEGIN

                  // split_strings[1] == CLIENT_ID                         //<-- may use this to validate who we got data for (must match ourselves, if other clients are using this port)
                  if (split_strings[1] == this->client_id_)
                  {
                    // good, as expected
                  }
                  else
                  {
                    // uh oh...
                    ATOMIC_CONSOLE_OUTPUT("Unexpected client response for [" << split_strings[1] << "]" << std::endl);
                    exit(-3);
                  }

                  // split_strings[2] == iteration                       
                  current_iteration = std::stoi(split_strings[2].c_str());

                  // split_strings[3] == iterations
                  // split_strings[4] == data_allocation time in NS        //<-- may use this to tally a rolling average/statistics for end result
                  // split_Strings[5] == elapsed system time NS            //<-- may use this to tally a rolling average/statistics for end result
                  // split_strings[6] == delta begin times NS

                  // split_strings[7] == data size to send
                  expected_data_payload_length = std::stoi(split_strings[7].c_str());

                  // split_strings[8] == sourceIP
                  // split_strings[9] == END|
                }
              }

              // provide analysis output of when we received the "start of test" tokens (and mark our local clock time of this event)
              {
                {
                  using namespace boost::chrono;
                  std::stringstream ss;
                  ss << "BEGIN" << token_divider << start_tokens << token_divider << sw_master.elapsed_ns();
                  test_results.push_back(ss.str());  // COPY
                }
              }

              actual_data_begin = p1 + start_tokens.length();
              actual_data_plus_header_begin = p1;

              current_test_index = q1+4;  //< 4 is length of "END|"
            }
            else
            {
              // the "start of test" tokens bleed over to the next a_small_read_buffer, so wait for more read to finish...
            }
          }
        }

        if (
          (current_test_index > 0)  //< a start token has been detected, to now start looking for the end token after all the data is received
        )
        {
          auto p2 = long_input_buffer.find(
            "|ANT_END",
            expected_data_payload_length+(minimum_server_initial_response_length+45)  //< if we know the number of bytes to expect, we can offset this amount during our search for the END token (45 represents the length of the BEGIN tokens)
          );
          if (p2 != std::string::npos)
          {
            auto q2 = long_input_buffer.find(
              "END|", 
              p2+9  //< 9 == lengthof "|ANT_END," + lengthof (token_divider)
            );  
            if (q2 != std::string::npos)
            {
              std::string end_tokens;  
              for (auto j = p2; j <= q2+3; ++j)
              {
                end_tokens += long_input_buffer.at(j);
              }

              {
                using namespace boost::chrono;
                std::stringstream ss;
                ss << "END" << token_divider << end_tokens << token_divider << sw_master.elapsed_ns();
                test_results.push_back( ss.str() );  // COPY
              }

              actual_data_end = p2 - 1;  //< the data ends at one character position before the end token
              actual_data_plus_footer_end = q2 + 3;

              ++completions_detected;

              current_test_index = 0;
            }
          }
          else
          {
          }
        }

        if (completions_detected_lag != completions_detected)
        {
          // the next xmit completion has been detected...

          Stopwatch w;
          for (auto i = actual_data_begin; i <= actual_data_end; ++i)
          {
            actual_data += long_input_buffer.at(i);  //< copy the actual data from the input buffer (this can be very slow for very large messages, >1MB)
          }
          {
            // Add how long it took to copy the input buffer into basically an application buffer
            // (emulate de-serializing the data into an object)
            std::stringstream ss;
            ss << token_divider << w.elapsed_ns();
            test_results.rbegin()->append(ss.str());
          }

          int count_to_erase = (actual_data_plus_footer_end - actual_data_plus_header_begin) + 1;
          long_input_buffer.erase(actual_data_plus_header_begin, count_to_erase);

          completions_detected_lag = completions_detected;  //< resync the lag to prepare for the next iteration

          {
            std::stringstream ss;
            ss << "DATA #" << completions_detected << " received (" << actual_data.length() << ")";

#if defined(_DEBUG)
            ss << "[";
            static const int max_sample_length = 30;
            if (actual_data.length() < max_sample_length)
            {
              ss << actual_data << "]";
            }
            else
            {
              for (int i = 0; i < max_sample_length; ++i)
              {
                ss << actual_data.at(i);
              }
              ss << "...]";
            }
#endif
            ss << std::endl;

            ATOMIC_CONSOLE_OUTPUT( ss.str() );

            {              
              std::stringstream ss;
              ss << "ANT_" << current_iteration << "_client_received.dat";

              std::ofstream ofs(ss.str());
              if (ofs.is_open())
              {
                ofs << actual_data;
                ofs.close();
              }
            }

          }

          actual_data.clear();
        }

        if (test_results.size() > 0)
        {
          std::stringstream ss;
          ss << "results.txt";  //< TODO: add timestamp

          std::ofstream ofs(ss.str(), std::fstream::out | std::fstream::app);
          if (ofs.is_open())
          {
            ofs << (*test_results.rbegin()) << std::endl;

            ofs.close();

            test_results.clear();
          }
        }

        if (completions_detected >= number_of_repetitions_)
        {
          ATOMIC_CONSOLE_OUTPUT("FINISHED" << std::endl << "[" << long_input_buffer << "]" << std::endl);

          /*
          auto tr_iter = test_results.begin();
          while (tr_iter != test_results.end())
          {
            ATOMIC_CONSOLE_OUTPUT( (*tr_iter) << std::endl );              

            ++tr_iter;
          }
          */
            
          break;
        }
        
      }
      // ***** ^^^ **************************************************************************
    }

    client_socket_->close();  // explicit close since the client is finished
  }
} 

template<class S>
void ANT_tcp_client<S>::Impl::handle_write(const boost::system::error_code& ec, size_t bytes_transferred)
{    
  ATOMIC_CONSOLE_OUTPUT("ANT_tcp_client::handle_write()" << std::endl);
}

template<class S>
ANT_tcp_client<S>::ANT_tcp_client(
  const S& host_name, 
  const S& port, 
  const S& client_id,
  int number_of_repetitions,
  int bytes_to_be_transmitted
)
:
  pimpl_(new Impl(
    client_id,
    number_of_repetitions,
    bytes_to_be_transmitted
  )),
  impl_(*pimpl_)
{
  ATOMIC_CONSOLE_OUTPUT("ANT_tcp_client::ANT_tcp_client(constructor)" << std::endl);

  boost::asio::ip::tcp::resolver::query query(host_name, port);     
  // ^^^ prepare a query for the host(server) we are interested in connecting to.
  //     This host(server) might not be available (no physical network path), not operational (not running), or
  //     even if it is connected it might be in a degraded(slow) state due to other network traffic beyond our control.

  ATOMIC_CONSOLE_OUTPUT("attempting to resolve host " << host_name << ":" << port << std::endl);

  impl_.resolver_.async_resolve(  //< note this is done async, and won't start until io_service run is invoked
    query, 
    boost::bind(
      &ANT_tcp_client::Impl::resolve_handler,  //< handler invoked upon resolving a host
      // --- corresponding parameters to the resolve_handler callback
      this,  //< instance
      boost::asio::placeholders::error,
      boost::asio::placeholders::iterator
    )
  );
}

template<class S>
ANT_tcp_client<S>::~ANT_tcp_client()
{
  ATOMIC_CONSOLE_OUTPUT("~ANT_tcp_client()" << std::endl);
}

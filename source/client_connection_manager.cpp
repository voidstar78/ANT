#include <ant/include/client_connection_manager.h>  //< This CPP is implementating interfaces specified in this interface.

// C++ STL std components used by this translation unit.
#include <string>
#include <fstream>

// boost C++ components used by this translation unit.
#define BOOST_SP_USE_QUICK_ALLOCATOR  // potential performance improvement mentioned in The Boost C++ Libraries 2nd Edition pg8
#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/array.hpp>
#include <boost/random.hpp>
#include <boost/make_shared.hpp>
#include <boost/atomic.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

// Application-specific includes relevant to this translation unit.
#include <ant/include/application_status.h>
#include <ant/include/ant_server.h>

namespace {

struct Null_deleter
{
  template<typename T> 
  void operator()(T*) 
  {
  }
};

void wait_a_moment()
{
  auto initial_check_time = ANT::sw_global_program_startup.elapsed_ms();
  ANT::Stopwatch::value_type delta(0);
  while ( delta < ANT::standard_spam_recurrence_time )
  {
    boost::this_thread::sleep(boost::posix_time::milliseconds(ANT::standard_spam_recurrence_time));
    // ^^ The above sleep should be the only delay necessary, but the surrounding loop is kept for reference.

    auto current = ANT::sw_global_program_startup.elapsed_ms();
    if (current < initial_check_time)  //< Rollover protection (and gives the loop something to do while waiting)
    {                
      initial_check_time = current;
    }

    delta = current - initial_check_time;
  }
}

}

namespace ANT {

struct Client_connection_manager::Impl
{
  // TYPE/CONSTANTS
  struct Data_transmit_task
  {
    Data_transmit_task(boost::asio::ip::tcp::socket& a_socket) 
    : 
      associated_socket(a_socket),  // pointer back to the socket that will be used to transmit the data for this transmit task
      iterations(-1),               // -1 is used to indicate uninitialized 
      data_size_to_transmit(0)      // 0 is used to indicate uninitialized 
    { 
      APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
    }

    ~Data_transmit_task()
    {
      APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
    }

    void reset_for_this_interval()
    {
      total_bytes_transferred = 0;
    }

    boost::asio::ip::tcp::socket& associated_socket;

    std::string associated_client_name;  //< string mnenomic for reference of the client being processed by this instance

    unsigned int iterations;  //< number of iterations to conduct for this transmit task

    std::size_t data_size_to_transmit;  //< number of bytes per iteration for this transmit task

    boost::atomics::atomic_uint64_t total_bytes_transferred;

  };  

  static const char token_divider = ',';

  // IMPLEMENTATION INTERFACES
  Impl(Connection_ID_type assigned_connection_id);
  ~Impl();

  inline static boost::asio::io_service& application_io_service() 
  { 
    static boost::asio::io_service io_service; 
    return io_service; 
  }

  void handle_initial_command_read(const boost::system::error_code& e, std::size_t bytes_transferred);   
  void handle_transmit_task_async_send(const boost::system::error_code& e, std::size_t bytes_transferred);

  void wait_inbetween_interval();

  void process_current_transmit_task();
  
  bool process_client_command();

  // DATA
  boost::asio::ip::tcp::socket socket_;

  Connection_ID_type connection_id_;
  std::string client_prefix_str_;
 
  boost::atomic<bool> writing_error_;
  boost::atomic<bool> initial_command_error_;
  boost::atomic<bool> finished_processing_client_command_;
  boost::atomic<bool> explicit_stop_issued_;

  Program_options& program_options_;

  boost::mutex mutex_accumulated_command_from_client_;
  std::string accumulated_command_from_client_;

  typedef std::vector<char> Read_buffer;
  typedef boost::scoped_ptr< Read_buffer > Read_buffer_SP;
  Read_buffer_SP a_small_socket_reader_buffer_;

  Data_transmit_task dtt_;

  boost::mutex mutex_write_to_transfer_log_;

  boost::atomic<unsigned int> async_send_finished_;
  unsigned int async_send_issued_;

};

Client_connection_manager::Impl::Impl(Connection_ID_type assigned_connection_id)
:
  socket_(application_io_service()),
  connection_id_(assigned_connection_id),
  writing_error_(false),
  initial_command_error_(false),
  finished_processing_client_command_(false),
  explicit_stop_issued_(false),
  program_options_(Program_options::instance()),
  dtt_(socket_),
  async_send_finished_(0),
  async_send_issued_(0)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);  

  {
    std::stringstream ss;
    ss << "CID" << connection_id_ << "[??]";  //< We don't yet know the assigned client ID or the client name, just put placeholders for now

    client_prefix_str_ = ss.str();
  }
}

Client_connection_manager::Impl::~Impl()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void Client_connection_manager::Impl::handle_initial_command_read(const boost::system::error_code& e, std::size_t bytes_transferred)
{
  if (e)
  {
    APPLICATION_STATUS(client_prefix_str_ << ": Error (" << e << ") while attempting to handle initial command" << std::endl);
    initial_command_error_ = true;
  }
  else if (bytes_transferred > 0)
  {
    {
      boost::lock_guard<boost::mutex> locked_accumulated_command_from_client(mutex_accumulated_command_from_client_);
        
      accumulated_command_from_client_.append(a_small_socket_reader_buffer_->data(), 0, bytes_transferred);
    }

    if (process_client_command())
    {
      APPLICATION_STATUS(client_prefix_str_ << ": Done processing client command" << std::endl);
      finished_processing_client_command_ = true;
    }
    else
    {
      // Assume we got a partial command and that more of the command is coming in the next read....
      int a = 0;  //< Breakpoint placeholder
    }
  }
}

void Client_connection_manager::Impl::handle_transmit_task_async_send(const boost::system::error_code& e, std::size_t bytes_transferred)
{
  if (e)
  {
    writing_error_ = true;
  }
  else if (bytes_transferred > 0)
  {
    dtt_.total_bytes_transferred += bytes_transferred;

    ++async_send_finished_;

    APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Sent " << bytes_transferred << " bytes" << std::endl);
  }  
}

void Client_connection_manager::Impl::wait_inbetween_interval()
{
  if (program_options_.wait_time_between_intervals > 0)
  {
    auto started_to_wait( sw_global_program_startup.elapsed_ms() );
    auto current(started_to_wait);  //< COPY  
    while (true)
    {
      current = sw_global_program_startup.elapsed_ms();

      if (current < started_to_wait)  //< Rollover protection
      {
        started_to_wait = sw_global_program_startup.elapsed_ms();
        APPLICATION_STATUS("Global timer roll over detected, wait-time may not be honored correctly" << std::endl);
      }
      else
      {
        auto delta = current - started_to_wait;
        if (delta >= program_options_.wait_time_between_intervals)
        {
          break;
        }

        boost::this_thread::sleep(boost::posix_time::milliseconds(program_options_.wait_time_between_intervals));
      }
    }
  }
}

void Client_connection_manager::Impl::process_current_transmit_task()
{
  APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Processing transmit task of " << dtt_.data_size_to_transmit << " bytes (iterations " << dtt_.iterations << ")" << std::endl);

  static const auto beep_sleep_time = boost::posix_time::microseconds(100);

  // ****************************************
  // RNG = Random Number Generator
  // These RNG declarations are only used if we are NOT using the file-mode program option, otherwise
  // we populate the transmitted data buffer using read-in portions of the specified file instead of
  // randomly generating alphanumerics.
  typedef boost::mt19937 RNG_type;  //< No specific reason to use mt19937, just was the one picked
  RNG_type rng( static_cast<uint32_t>( time(0)) );  //< use time-now to seed the Random Number Generator
  boost::uniform_int<> one_to_three( 1, 3 );
    
  boost::variate_generator< 
    RNG_type, 
    boost::uniform_int<> 
  > dice(rng, one_to_three);

  char char_generated(' ');  //< allocated here, to avoid re-allocating it over and over inside the for-loop
  int die_role(-1);  //< allocated here, to avoid re-allocating it over and over inside the for-loop
  // ----------------------------------------
  // This IFS (Input File Stream) is only used if the file-mode program option is enabled, to represent
  // the handle to the file used for the data transfer test (if specified).
  boost::scoped_ptr< std::ifstream > ifs(0);
  // ****************************************
    
  uint64_t previous_begin_offset(0);  //< Initially there is no "previous" BEGIN, so implicitly it will have an offset of 0.  Subsequent iterations will then update this "previous" accordingly.  

  if (program_options_.send_filename.length() > 0)
  {
    // Override the Data Transmit Task bytes to send with whatever the content of the given filename is.

    // NOTE: The file length was determined during program options validation.  This does assume that the file is not modified externally
    // after starting ANT.
    
    dtt_.data_size_to_transmit = program_options_.send_file_length;    
  } 

#if defined(_DEBUG)
  // For convenience in debugging, we'll prepare a HEAD and TAIL buffer to store a brief portion
  // of the transmitted data.  If the test size is small (e.g. <20 bytes) then the entire
  // test data sent is display.  This allows a quick sanity check that the client has received
  // what the server sent (in both length and content).
  static const int max_sent_data_sample_len( 30 );
  typedef std::vector<char> Sent_data_sample;
  Sent_data_sample sent_data_sample_head;
  Sent_data_sample sent_data_sample_tail;
#endif

  boost::shared_ptr<char[]> raw_data(0);
  if (program_options_.enable_rapid_mode)
  { 
    raw_data.reset( rapid_data.get(), Null_deleter() );
    // ^^ We want all iterations to use the same rapid_data instance, along with all client connections.
    //    At this point the rapid_data is READ-ONLY and should not be de-allocated when this client
    //    connection is finished (unlike regular raw_data which is brand new per-client-connection data).

    APPLICATION_STATUS(client_prefix_str_ << ": RAPID-MODE enabled" << std::endl);
  }

  auto& current_end_point( dtt_.associated_socket.local_endpoint() );  //< Used while sending both the HEADER/FOOTER
  std::string ss_TOKENS_COMMON;
  {
    std::stringstream ss;
    ss <<
      (boost::format("%-40s") % dtt_.associated_client_name) << token_divider <<     // max 40 chars
      (boost::format("%15s") % current_end_point.address().to_string()) << ":" << 
      (boost::format("%05u") % current_end_point.port()) << token_divider <<
      (boost::format("%06u") % dtt_.iterations) << token_divider <<                  // max 999999 (6 digit)
      (boost::format("%012u") % dtt_.data_size_to_transmit) << token_divider;      //       8,589,934,592

    ss_TOKENS_COMMON = ss.str();
  }

  std::string ss_HEADER_FIXED;
  {
    std::stringstream ss;
    ss <<
      HEADER_BEGIN_TOKEN << token_divider <<
      ss_TOKENS_COMMON <<
      (boost::format("%015u") % this->connection_id_) << token_divider;            // max 999999 (6 digit)  This is 015 to match the FOOTER spacing, though that is overly generous in the HEADER context.        

    ss_HEADER_FIXED = ss.str();
  }

  std::string ss_FOOTER_FIXED;
  {
    std::stringstream ss;
    ss <<
      FOOTER_BEGIN_TOKEN << token_divider <<
      ss_TOKENS_COMMON;

    ss_FOOTER_FIXED = ss.str();
  }

  // Repeat the transmission for the number of commanded iterations...
  for (unsigned int iteration_index( 0 ), iteration_indexP1( 1 ); iteration_index < dtt_.iterations; ++iteration_index, ++iteration_indexP1)
  {
    unsigned int beeps( 0 );
    std::size_t bytes_logged_this_iteration(0);
    boost::shared_ptr<std::ofstream> log_ofs(0);  //< Used if data transfer logging is enabled.
    dtt_.reset_for_this_interval();
    unsigned int percentage_lag( 0 );    
    uint64_t elapsed_time_this_interval_to_allocate_and_prepare_send_data( 0 );

    if (program_options_.enable_log_to_file)
    {
      // Establish the log filename for the current interval...
      std::stringstream ss;
      ss << "ANT_" << (boost::format("%06u") % (iteration_index+1)) << "_of_" << (boost::format("%06u") % dtt_.iterations) << "_" << dtt_.associated_client_name << "_server_sent_CID" << this->connection_id_ << "_" << dtt_.associated_client_name << ".dat";      

      std::string log_filename( ss.str() );

      // Prepare an output stream to write/append the data being transmitted...
      log_ofs.reset( 
        new std::ofstream(
          log_filename, 
          std::ofstream::binary | std::ofstream::trunc 
        ) 
      );  // create or trunc the file
      
      if (log_ofs && log_ofs->is_open())
      {
        APPLICATION_STATUS(client_prefix_str_ << ": Transfer log for iteration " << iteration_indexP1 << " opened as [" << log_filename << "]" << std::endl);       
        // log_ofs remains allocated and opened for the duration of the tests.  Each client connection has a unique
        // log file based on ConnectionID.
      }
      else
      {
        APPLICATION_STATUS(client_prefix_str_ << ": Transfer log for iteration " << (iteration_index+1) << " could not be opened (log will not be written)" << std::endl);
        log_ofs = 0;
      }
    }       

    auto current_begin_offset_ns( sw_global_program_startup.elapsed_ns() );
    {                
      std::stringstream ss_begin;
      ss_begin << 
        ss_HEADER_FIXED <<
        // --- dynamic part is below
        (boost::format("%06u") % iteration_indexP1) << token_divider <<                // max 999999 (6 digit)
        (boost::format("%015u") % current_begin_offset_ns) << token_divider <<         // 000,000,000,000,000  (nano -> micro -> milli -> seconds)
        (boost::format("%015u") % ((iteration_index == 0) ? 0 : (current_begin_offset_ns - previous_begin_offset))) << token_divider <<
        HEADER_END_TOKEN;

      try
      {
        boost::system::error_code ec;
        auto actual_bytes_sent = socket_.send(boost::asio::buffer(ss_begin.str()), 0, ec);
        // ^^ Because the HEADER tokens is relatively short (~100 bytes), we attempt to send it in a synchronous call.
        // QUESTION: If this send hangs (e.g. power failure of this client), how will that impact other clients?

        if (ec || (actual_bytes_sent < 1) || (actual_bytes_sent > expected_header_length))
        {
          APPLICATION_STATUS(client_prefix_str_ << ": Error encountered attempting to send HEADER (iteration " << iteration_indexP1 << ")" << std::endl);
          return;
        }

        APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Sent HEADER [" << ss_begin.str() << "]" << std::endl);
      }
      catch (...)
      {
        APPLICATION_STATUS(client_prefix_str_ << ": Exception occurred while attempting to send data-transfer HEADER (iteration " << iteration_indexP1 << ")" << std::endl);
        return;
      }
    }
    previous_begin_offset = current_begin_offset_ns;  //< Update the offset time since the previous iteration (units: nanoseconds)

    // Note there is a difference between the amount of bytes prepared/generated to be sent and the 
    // amount that have actually been sent.
    std::size_t total_bytes_prepared_to_be_sent( 0 );
    while (dtt_.total_bytes_transferred < dtt_.data_size_to_transmit)  //< While we haven't send all the data corresponding to this iteration yet...
    {
      if (explicit_stop_issued_)  //< Explicit CTRL-C of TERM signalled
      {
        socket_.get_io_service().stop();
        return;
      }

      socket_.get_io_service().poll();  //< This is necessary to also handle other client connections.

      auto pending_async_sends = (async_send_issued_ - async_send_finished_);  //< Note, this fails of async_send_issued_ rolls over from MAXINT to 0.
      
      bool still_preparing_data = (total_bytes_prepared_to_be_sent < dtt_.data_size_to_transmit);
      if (
        (
          (program_options_.max_buffer_depth == 0)                         // unlimited depth...
          || (pending_async_sends < program_options_.max_buffer_depth)     // OR the amount pending is below our depth threshold...
        )
        && still_preparing_data                                            // and we still have bytes to be prepared...
      )
      {
        Stopwatch sw_allocate_and_prepare_send_data_start_time;  //< Prepare to time how long it took to generate this chunk of transmit data...

        size_t amount_of_data_generated_for_this_chunk( 0 );

        if (program_options_.enable_rapid_mode)
        {
          total_bytes_prepared_to_be_sent += program_options_.internal_buffer_length;
          amount_of_data_generated_for_this_chunk += program_options_.internal_buffer_length;

          if (total_bytes_prepared_to_be_sent > dtt_.data_size_to_transmit)
          {
            // Just in case we do overshoot, this backs off on the final
            // transmission so that we send only exactly what is expected (even if we ended up generating
            // and preparing more than what was expected).
            auto excess = total_bytes_prepared_to_be_sent - dtt_.data_size_to_transmit;
            total_bytes_prepared_to_be_sent -= excess;
            amount_of_data_generated_for_this_chunk -= excess;
          }
        }
        else  //< FILE-MODE or RANDOM-GENERATED...
        {
          raw_data = boost::make_shared<char[]>(program_options_.internal_buffer_length);
          // ^^ Prepare a new internal working buffer for each transmission.  After allocation, this 
          //    buffer must be populated with the next set of data to be sent to the clients.  The 
          //    populated buffer will be shared with a thread that writes the data transfer log (if enabled).          
       
          while (true)  
          {
            if (program_options_.send_filename.length() > 0)
            {
              // The send file is used for sending data to all connected clients.  Any
              // client may be in the process of reading their respective portion of the file.
              // In addition, the file system itself may become unavailable (removable media,
              // hardware errors, latent networks, etc.).  For this reason, we will attempt
              // to re-open the file multiple times.  The loading and initialization of
              // test data is intended to be external to the send/receive timing metrics.
try_again_active:
              ifs.reset( 
                new std::ifstream(
                  program_options_.send_filename, 
                  std::ifstream::binary
                ) 
              );

              if (ifs && ifs->is_open())
              {
                std::size_t bytes_read(0);
                try
                {
                  // NOTE: If the user has specified a large internal buffer, this could
                  // be a length processing.  But we can not thread this since we must
                  // have all the data allocated prior to sending it.  That's not entirely
                  // true since we could "chunk" up our internal buffer, but the
                  // internal buffer is already intended to be relatively small and represent
                  // a reasonable "chunk" size.
                  ifs->seekg(total_bytes_prepared_to_be_sent);  //< Offset the read to start past what we have read already (if any)
                  ifs->read( raw_data.get(), program_options_.internal_buffer_length );

                  bytes_read = ifs->gcount();

                  ifs->close();  //< So that the same sendfile can be used by other client connections
                  ifs.reset(0);
                }
                catch (...)
                {
                  APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Critical error while trying to read the send file" << std::endl);
                  goto try_again_handling;
                }

                if (bytes_read)
                {
                  total_bytes_prepared_to_be_sent += bytes_read;
                  amount_of_data_generated_for_this_chunk += bytes_read;
                }
                else
                {
                  goto try_again_handling;
                }
              }
              else
              {
try_again_handling:
                wait_a_moment();

                APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Retrying the read on the send file to prepare the send buffer" << std::endl);

                goto try_again_active;
              }
            }
            else  //< "randomly" generate some alpha-numeric sample data for the transfer test
            {
              // Randomly decide the value/sequence of the next byte in the data (note: this is not completely random, but the main 
              // intent is to have variability to attempt to negate artificial boost in performance due to any caches
              // along the chain of execution in transmitting data)      

              die_role = dice();  
        
              switch (die_role)
              {
              case 1:  // digit 0-9
                char_generated = static_cast<char>(48 + (amount_of_data_generated_for_this_chunk % 10));  // 48 is ASCII for '0'
                break;

              case 2:  // lower-case alphabet character
                char_generated = static_cast<char>(97 + (amount_of_data_generated_for_this_chunk % 26));  // 97 is ASCII for 'a'
                break;

              case 3:  // upper-case alphabet character
                char_generated = static_cast<char>(65 + (amount_of_data_generated_for_this_chunk % 26));  // 65 is ASCII for 'A'
                break;

              default:
                char_generated = ' ';
                break;
              }

              raw_data[amount_of_data_generated_for_this_chunk] = char_generated;

#if defined(_DEBUG)
              if (total_bytes_prepared_to_be_sent < max_sent_data_sample_len)
              {
                sent_data_sample_head.push_back(char_generated);
              }
              if (total_bytes_prepared_to_be_sent >= (dtt_.data_size_to_transmit - max_sent_data_sample_len))
              {
                sent_data_sample_tail.push_back(char_generated);
              }
#endif

              ++total_bytes_prepared_to_be_sent;
              ++amount_of_data_generated_for_this_chunk;        
            }  //< End else of randomly generated data

            if (
              (total_bytes_prepared_to_be_sent >= dtt_.data_size_to_transmit)  //< If exceeded the total amount of bytes to be transferred...
              || (amount_of_data_generated_for_this_chunk >= program_options_.internal_buffer_length)  //< or exceeded the amount we can fit into this write buffer...
              || (this->explicit_stop_issued_)
            )
            {
              if (program_options_.status_rate < 1)
              {
                std::stringstream ss;
                ss << client_prefix_str_ << ": " << total_bytes_prepared_to_be_sent;
                if (program_options_.max_client_connections == 1)
                {
                  ss << "\r";
                }
                else
                {
                  ss << std::endl;
                }
                APPLICATION_STATUS(ss.str());
              }
              else if (program_options_.status_rate < 21)
              {
                unsigned int percentage( static_cast<unsigned int>(static_cast<double>(total_bytes_prepared_to_be_sent) / static_cast<double>(dtt_.data_size_to_transmit) * 100.0) );

                if (percentage != percentage_lag)
                {
                  if ((percentage % program_options_.status_rate) == 0)
                  {
                    APPLICATION_STATUS(client_prefix_str_ << ": Iteration " << iteration_indexP1 << " is " << percentage << "% complete" << std::endl);
                  }
                  percentage_lag = percentage;
                }
              }
              else
              {
                // Don't report any progress status.
              }

              break;
            }
          }  // end while(true)
        }

        elapsed_time_this_interval_to_allocate_and_prepare_send_data += sw_allocate_and_prepare_send_data_start_time.elapsed_ns();
      
        try
        {      
        
          {
            socket_.async_send(
              boost::asio::buffer(raw_data.get(), amount_of_data_generated_for_this_chunk),
              boost::bind(
                &Client_connection_manager::Impl::handle_transmit_task_async_send,
                // ----------------------------------
                this,  
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
              )
            );

            ++async_send_issued_;

            socket_.get_io_service().poll();  //< Perform any async_send
          }

          if (program_options_.enable_log_to_file) 
          {
            // The data transfer log is expected to remain open for the duration of the data transfer test, 
            // so that it is appended during each write-cycle.
            if (log_ofs && log_ofs->is_open())
            {
              boost::thread thread(
                [=, &bytes_logged_this_iteration]() 
                {
                  boost::lock_guard<boost::mutex> locked_write_to_transfer_log(mutex_write_to_transfer_log_);                  

                  log_ofs->write(raw_data.get(), amount_of_data_generated_for_this_chunk);  

                  bytes_logged_this_iteration += amount_of_data_generated_for_this_chunk;
                  
                  if (bytes_logged_this_iteration >= dtt_.data_size_to_transmit)
                  {
                    // The expeceted number of bytes has been written to the data transfer log, we can
                    // close the output stream.
                    log_ofs->close();
                  }                                    
                }
              );              
            }
            else
            {
              APPLICATION_STATUS(client_prefix_str_ << ": Log of data transfer sent is not allocated or available (send log will be inconsistent with the receive log, iteration " << iteration_indexP1 << ")" << std::endl);
            }
          }
        }
        catch (...)
        {
          APPLICATION_STATUS(client_prefix_str_ << ": Exception occurred while attempting to send a data chunk (iteration " << iteration_indexP1 << ")" << std::endl);
          return;
        }
      }
      else
      {        
        socket_.get_io_service().poll();  //< Perform any async_send

        // Sleep the application thread, nothing else to do right now (waiting for the actual async sends to finish)        
        boost::this_thread::sleep(beep_sleep_time);

        if (still_preparing_data)
        {
          // We got here because we ran out of DEPTH space (i.e. we would have
          // prepared more data, but the DEPTH threshold prevents us from
          // using more memory and queing another async_send)
          ++beeps;
        }
        else
        {
          // We've prepared all the data, just waiting for the async_sends to finish
        }
      }

      if (writing_error_)
      { 
        APPLICATION_STATUS(client_prefix_str_ << ": Error encountered while sending data (iteration " << iteration_indexP1 << ")" << std::endl);
        return;
      }

    }  //< end (dtt_.total_bytes_transferred < dtt_.data_size_to_transmit) -- keep sending portions of data until reached the test payload objective    

    {
      //auto send_delta( dtt_.async_send_end_time - dtt_.async_send_start_time );  ABC BROKE

      auto current_end_offset_ns( sw_global_program_startup.elapsed_ns() );
      std::stringstream ss_end;
      ss_end << 
        ss_FOOTER_FIXED <<
        // -- dynamic part
        "000000000000000" << token_divider <<
        (boost::format("%06u") % iteration_indexP1) << token_divider <<
        (boost::format("%015u") % (current_end_offset_ns - current_begin_offset_ns)) << token_divider <<
        (boost::format("%015u") % elapsed_time_this_interval_to_allocate_and_prepare_send_data) << token_divider <<                        
        FOOTER_END_TOKEN;

      try
      {
        boost::system::error_code ec;
        auto actual_bytes_sent = socket_.send(boost::asio::buffer(ss_end.str()), 0, ec);  
        // ^^ Because the FOOTER tokens is relatively short (~100 bytes), we attempt to send it in a synchronous call.
        // QUESTION: If this send hangs (e.g. power failure of this client), how will that impact other clients?

        if (ec || (actual_bytes_sent < 1) || (actual_bytes_sent > expected_footer_length))
        {
          APPLICATION_STATUS(client_prefix_str_ << ": Error encountered attempting to send FOOTER (iteration " << iteration_indexP1 << ")" << std::endl);
          return;
        }

        APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Sent FOOTER [" << ss_end.str() << "]" << std::endl);
      }
      catch (...)
      {
        APPLICATION_STATUS(client_prefix_str_ << ": Exception occurred while attempting to send data-transfer FOOTER (iteration " << iteration_indexP1 << ")" << std::endl);
        return;
      }
    }

    {
      std::stringstream ss;
      ss << client_prefix_str_ << ": Finished iteration " << (iteration_index+1) << " of " << dtt_.iterations << " (" << dtt_.total_bytes_transferred << " bytes sent) [" << beeps << "]";
#if defined(_DEBUG)
      ss << "[";
      auto sds_iter( sent_data_sample_head.begin() );
      while (sds_iter != sent_data_sample_head.end())
      {
        ss << *sds_iter;
        ++sds_iter;
      }
      if (dtt_.data_size_to_transmit > max_sent_data_sample_len*2)  //< *2 because HEAD contains a sample and TAIL also contains a sample of the same length
      {
        ss << "...";
        sds_iter = sent_data_sample_tail.begin();
        while (sds_iter != sent_data_sample_tail.end())
        {
          ss << *sds_iter;
          ++sds_iter;
        }
      }
      ss << "]";

      sent_data_sample_head.clear();
      sent_data_sample_tail.clear();
#endif
      APPLICATION_STATUS(ss.str() << std::endl);
    }

    wait_inbetween_interval();

    if (program_options_.enable_log_to_file)
    {
      while (true)
      {
        if (log_ofs->is_open())
        {
          APPLICATION_STATUS(client_prefix_str_ << ": Waiting for log file to be closed" << std::endl);
          boost::this_thread::sleep(boost::posix_time::milliseconds(standard_spam_recurrence_time));
        }
        else
        {
          break;
        }
      }
    }
  }  // end for-each-test iteration  

  // Since we've finished all our data transmission, the client may have extra reads
  // queued up.  Try to flush those out before exiting...
  std::string done_str("END");
  boost::system::error_code ec;
  while (true)
  {    
    auto n = socket_.send(
      boost::asio::buffer(done_str), 
      0,  //< flags
      ec
    );
    if (ec)
    {
      // We're waiting for the client to disconnect, to flush out buffered up reads...
      break;
    }
  }  

  while (async_send_finished_ < async_send_issued_)
  {
    APPLICATION_STATUS(".");
  }

}

bool Client_connection_manager::Impl::process_client_command()  //< return TRUE if a command was successfully parsed and executed
{
  bool result( false );  //< Assume no command or the parsing failed or failed to initiate/execute the command
  
  std::string command_from_client;
  {
    boost::lock_guard<boost::mutex> locked_accumulated_command_from_client(mutex_accumulated_command_from_client_);
    command_from_client = this->accumulated_command_from_client_;  //< STRING COPY
  }

  if (
    (command_from_client.length() >= minimum_command_length)  
    && (command_from_client.find(COMMAND_END_TOKEN, min_command_end_offset) != std::string::npos)
  )
  {
    typedef std::vector<std::string> Split_strings;
    Split_strings split_strings;
    boost::split(
      split_strings,         //< output to where the split strings will be placed
      command_from_client,   //< the test_command that we received from the client (note: the FULL command is assumed to fit within our "small reader buffer")
      [](char c) { return (c == token_divider); }  //< told that this is "more efficient" than passing a function call (but the syntax makes the intent obscure, IMO)
    );

    static const unsigned int expected_minimum_string_items( 5 );
    if (split_strings.size() >= expected_minimum_string_items)  //< Make sure the command got split into the minimum number of items
    {
      static const int begin_token_index( 0 );
      static const int end_token_index( 4 );
      if (
        (split_strings[ begin_token_index ].find(COMMAND_BEGIN_TOKEN) != std::string::npos)
        && (split_strings[ end_token_index ].find(COMMAND_END_TOKEN) != std::string::npos)
      )
      {        
        // Tentatively this appears to be a valid command since the proper BEGIN and END tokens are present.
        // Now check the contents of the command...
        result = true;

        dtt_.associated_client_name = split_strings[1];  // STRING COPY
        if (dtt_.associated_client_name.length() > max_client_name_length)
        {
          result = false;
        }

        {
          std::stringstream ss;
          ss << split_strings[2].c_str();  //< N count, number of times to do the transmit test (iterations count)  
          ss >> dtt_.iterations;
        }
        if (dtt_.iterations > 999999)
        {
          result = false;
        }

        {
          std::stringstream ss;
          ss << split_strings[3].c_str();  //< dataSize to send (bytes) 
          ss >> dtt_.data_size_to_transmit;
        }
        if (dtt_.data_size_to_transmit > max_transfer_test_size_allowed)
        {
          result = false;
        }

        {
          std::stringstream ss;
          ss << "CID" << this->connection_id_ << "[" << dtt_.associated_client_name << "]";

          client_prefix_str_ = ss.str();
        }

        APPLICATION_STATUS(client_prefix_str_ << ": Command parsed (" << dtt_.iterations << "x" << dtt_.data_size_to_transmit << " bytes to be sent to client) ");
        if (program_options_.send_file_length > 0)
        {
          APPLICATION_STATUS("[FILE-MODE overrides size to " << program_options_.send_file_length << "]" << std::endl);
        }
        else
        {
          APPLICATION_STATUS(std::endl);
        }

        process_current_transmit_task();
      }      
    }
    else
    {
      APPLICATION_STATUS(client_prefix_str_ << ": Command not yet fully formed (insufficient split items)" << std::endl);
    }
  }
  else
  {
    APPLICATION_DEBUG_STATUS(client_prefix_str_ << ": Partial command buffered, waiting for the remainding portion" << std::endl);
  }

  return result;
}

Client_connection_manager::Client_connection_manager(Connection_ID_type assigned_connection_id)
: 
  pimpl_(
    new Impl(assigned_connection_id)
  ),
  impl_(*pimpl_)
{
  APPLICATION_DEBUG_STATUS(impl_.client_prefix_str_ << ": " << __FUNCTION__ << " usr @ " << this << std::endl);
}

Client_connection_manager::Client_connection_manager(const Client_connection_manager& other_ccm)
:
  pimpl_(other_ccm.pimpl_),
  impl_(other_ccm.impl_)
{
  APPLICATION_DEBUG_STATUS(impl_.client_prefix_str_ << ": " << __FUNCTION__ << " cpy @ " << this << std::endl);
}

Client_connection_manager::~Client_connection_manager()
{
  APPLICATION_DEBUG_STATUS(impl_.client_prefix_str_ << ": " << __FUNCTION__ << " bb @ " << this << std::endl);

  //impl_.per_iteration_data_.clear();  
  // ^^ Explicit flush the shared pointers in the client send buffer.  vector does not explicitly destruct each of its elements 
  //    upon its own de-allocation.
}

void Client_connection_manager::start()  //< start the main workflow of this server connection, which is to wait for a transmit command and then execute that command
{
  // Wait for the initial ANT Test Command from this client...  
  Impl::Read_buffer* read_buffer_raw_pointer( new Impl::Read_buffer() );  //< allocated to the heap

  if (read_buffer_raw_pointer)
  {    
    read_buffer_raw_pointer->resize( modest_maximum_of_client_command_length );

    // Wait for a command from the client.  The first immediate thing the ANT client is expected
    // to do upon connection is to send the ANT Test Command.
      
    impl_.a_small_socket_reader_buffer_.reset( read_buffer_raw_pointer );
    if (impl_.a_small_socket_reader_buffer_)
    {
      static const std::size_t max_time_to_wait_for_command_ms(900);
      APPLICATION_STATUS(impl_.client_prefix_str_ << ": Waiting for client command (will wait up to " << max_time_to_wait_for_command_ms << "ms)" << std::endl);

      // Read the initial or the next "chunk" containing the remainder of the command...
      impl_.socket_.async_read_some(
        boost::asio::buffer(*impl_.a_small_socket_reader_buffer_), 
        boost::bind(
          &Client_connection_manager::Impl::handle_initial_command_read, 
          // ------------------------------
          pimpl_, 
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred
        )
      ); 

      Stopwatch sw_waiting_for_command_start;
      do  //< wait for transmit task commands
      {               
        impl_.socket_.get_io_service().poll();

        // Interpret what was read to see if there is some client command therein
/*
Minimal example: "|ANT,A,10,1024,END|"   (interpret as: Client called "A" sending transmit request of 1 byte to be sent 1 time)
note: ",END" is an ending token to indicate the end of the command (and no more content in the data stream is expected from the client)
*/                
        
        if (impl_.initial_command_error_)
        {
          APPLICATION_STATUS(impl_.client_prefix_str_ << ": Error while trying to process ANT Test Command from client" << std::endl);
          break;
        }
        else if (impl_.finished_processing_client_command_)
        {
          break;
        }
        else
        {                    
          if (sw_waiting_for_command_start.elapsed_ms() > max_time_to_wait_for_command_ms)          
          {
            APPLICATION_STATUS("Client command not received or completed within " << max_time_to_wait_for_command_ms << "ms" << std::endl);
            break;
          }
          // else - assume we need to read more to get the full command
        }

        if (impl_.explicit_stop_issued_)
        {
          break;
        }
    
      } while (true);
      
    }  // end if (impl_.a_small_socket_reader_buffer)
    else
    {
      APPLICATION_STATUS(impl_.client_prefix_str_ << "Unable to allocate pointer of socket read buffer" << std::endl);
    }
    // --- end scope for a_small_socket_reader_buffer
  }
  else
  {
    APPLICATION_STATUS(impl_.client_prefix_str_ << "Unable to allocate socket read buffer" << std::endl);
  }
}

void Client_connection_manager::explicit_stop()
{
  impl_.explicit_stop_issued_ = true;
}

Client_connection_manager::Client_connection_manager_SP Client_connection_manager::create( Connection_ID_type assigned_connection_id )
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  auto& program_options(Program_options::instance());

  Client_connection_manager* client_connection_manager_ptr( new Client_connection_manager(assigned_connection_id) );  //< Defer this allocation as long as possible, past anything prior that could wrong (so we don't have to deal with manually de-allocating this if anything did go wrong)

  if (client_connection_manager_ptr)  //< Defensive programming sanity-check.  We would have to be in a poor runtime situation for this to actually fail.
  {  
    APPLICATION_DEBUG_STATUS("CID" << client_connection_manager_ptr->connection_id() << " created @ " << client_connection_manager_ptr << std::endl);
    APPLICATION_STATUS("Waiting for next client connection (will be assigned as CID" << client_connection_manager_ptr->connection_id() << ")" << std::endl);
  }
  else
  {
    client_connection_manager_ptr = 0;  //< This is really just a breakpoint position to monitor for this case, which should ordinarily never occur or be necessary.
  }

  return Client_connection_manager::Client_connection_manager_SP(client_connection_manager_ptr);  //< COPY the raw pointer and prepare the shared-pointer accounting
}

Connection_ID_type Client_connection_manager::connection_id() const 
{ 
  return impl_.connection_id_; 
}

const boost::asio::ip::tcp::socket& Client_connection_manager::socket() const 
{ 
  return impl_.socket_; 
}

boost::asio::ip::tcp::socket& Client_connection_manager::socket() 
{ 
  return impl_.socket_; 
}

}  //< end namespace ANT

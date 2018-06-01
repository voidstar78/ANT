#include <string>
#include <iostream>

#if defined(WIN32)
#include <windows.h>  //< Used for the call to SetProcessWorkingSetSize
#endif

#include <ant/include/ant_client.h>                        //< This is the interface to the ANT Client application.
#include <ant/include/ant_server.h>                        //< This is the interface to the ANT Server application.
#include <ant/include/application_status.h>                //< Application specific utility for a consistent means of providing end user with status.
#include <ant/include/common_program_options_handler.h>    //< Application specific utility for handling user-defined program option configuration.
#include <ant/include/common_types.h>                      //< Application specific global interfaces (Program_options, constants, etc.)

int main(int argc, char** argv)
{
  // Told that the following MUST get executed before ANY other
  // stringstream/console related calls in order to have effect.  So
  // caution on declaring other statics that get initialized prior
  // to main.
  std::ios_base::sync_with_stdio(false);  // This application does not use stdio
  std::cin.tie(0);  // This application has no user/console input

  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  static const std::string application_version("0.1.7");
  APPLICATION_STATUS(
    "A Network (Performance) Tester v" << application_version << std::endl
  );  

  auto total_detected_system_memory(ANT::total_system_memory());
  ANT::max_transfer_test_size_allowed = total_detected_system_memory / 2;  //< For benchmarking purely RAM transfers, the max test should be limited to half of main physical RAM (but should be less if other processes are consuming large amounts of memory)

  try
  {
    APPLICATION_STATUS("Initializing program options..." << std::endl);
    ANT::initialize_program_options(  //< Reminder, this function has its own internal try-catch on std::exception.  (TODO: How would we know this if we weren't privy to this functions implementation?  Since we can't be certain across versions/implementations, we encapsulate with our own try-catch also)
      argc, argv
      );
  }
  catch (...)  //< Backup provision in case something very unusual happens
  {
    APPLICATION_STATUS("Unexpected error occurred while parsing command line options" << std::endl);
    APPLICATION_STATUS("Attempting to proceed (some program options may be defaulted)" << std::endl);
  }

  auto& program_options(ANT::Program_options::instance());

#if defined(WIN32)
  try
  {
    // Attempt to pre-allocate how much memory this application execution mode is going to use.
    size_t target_workingset_size(0);
    if (program_options.enable_server_mode)
    {
      if (program_options.enable_rapid_mode)
      {
        // In rapid mode, the internal buffer is initialized once (with exactly the same data) and that
        // same allocation is re-used during each iteration and by each client connection.
        target_workingset_size = program_options.internal_buffer_length;
      }
      else
      {
        target_workingset_size = (program_options.internal_buffer_length * program_options.max_buffer_depth) * program_options.max_client_connections;
      }
      // NOTE: There is some additional memory overhead for some accounting data associated with each client.
    }
    else
    {
      // In client mode, primarily just the internal working buffer and main data accumulated number of bytes is allocated
      // Each iteration should be independent, such that the accumulated data is de-allocated at the end of an iteration.
      // Though it is unclear if the working set size should be reset again after such a large de-allocation.
      target_workingset_size = program_options.bytes_to_be_transmitted + program_options.internal_buffer_length;

      // NOTE: Client has additional memory overhead of the statistics that is not accounted for in the above.
    }
    if (
      SetProcessWorkingSetSize(
        GetCurrentProcess(),
        static_cast<SIZE_T>((target_workingset_size)),  //< dwMinimumWorkingSetSize
        static_cast<SIZE_T>((target_workingset_size))   //< dwMaximumWorkingSetSize
        )
      )
    {
      APPLICATION_STATUS("Setting WorkingSetSize set to " << target_workingset_size << " bytes for this process" << std::endl)
    }
    else
    {
      APPLICATION_STATUS("Warning: The attempt to set the WorkingSetSize failed (target size was " << target_workingset_size << " bytes)" << std::endl)
    }
  }
  catch (...)
  {
    APPLICATION_STATUS("Unexpected error while attempting to set WorkingSetSize" << std::endl);
  }
#else
  APPLICATION_STATUS("Pre-allocation of WorkingSetSize (or equivalent) is not implemented" << std::endl);
#endif

  // Show options/status that is common between both client and server executing modes...
  {
    std::stringstream ss;
    if (program_options.enable_log_to_file)
    {
      ss << "ENABLED";
    }
    else
    {
      ss << "DISABLED";
    }
    APPLICATION_STATUS("Logging data transfer content to file..: " << ss.str() << std::endl);
  }

  {
    std::stringstream status_rate_desc_ss;
    if (program_options.status_rate == 0)
    {
      status_rate_desc_ss << "FASTEST";
    }
    else if (program_options.status_rate > 20)  //< 21, 22, ....
    {
      status_rate_desc_ss << "OFF";
    }
    else
    {
      status_rate_desc_ss << "MODULO";
    }
    APPLICATION_STATUS("Status tempo...........................: " << program_options.status_rate << " (" << status_rate_desc_ss.str() << ")" << std::endl);
  }

  APPLICATION_STATUS("Working Buffer size....................: " << program_options.internal_buffer_length << " bytes" << std::endl);

  APPLICATION_STATUS("NO_DELAY setting.......................: " << program_options.set_nodelay << std::endl);

  {
    double total_gb(static_cast<double>(total_detected_system_memory) / (1024*1024*1024));
    APPLICATION_STATUS("Detected total available system memory.: " << total_detected_system_memory << " bytes (" << total_gb << "GB)" << std::endl);
  }

  {
    auto ncores(boost::thread::hardware_concurrency());
    APPLICATION_STATUS("Detected number of cores...............: " << ncores << " (logical processors)" << std::endl);
  }

  try
  {
    if (program_options.enable_server_mode)
    {
      {
        std::stringstream ss;
        if (program_options.enable_rapid_mode)
        {
          ss << "RAPID-STATIC, fixed length and content buffer is sent to client";
        }
        else if (program_options.send_filename.length())
        {
          ss << "FILE-SEND, specified file is sent to client";
        }
        else
        {
          ss << "LINEAR-RANDOM, random alphanumeric sequence initialized linearly and sent to client";
        }
        APPLICATION_STATUS("Execution mode.........................: SERVER (" << ss.str() << ")" << std::endl);
      }

      APPLICATION_STATUS("Maximum simultaneous client connections: " << program_options.max_client_connections << std::endl);

      APPLICATION_STATUS("Maximum buffer depth...................: " << program_options.max_buffer_depth << std::endl);

      APPLICATION_STATUS("Wait time between client iterations....: " << program_options.wait_time_between_intervals << "ms" << std::endl);

      ANT::ANT_server ant_server;

      ant_server.run();

      APPLICATION_DEBUG_STATUS("ANT Server finished" << std::endl);
    }
    else
    {
      APPLICATION_STATUS("Execution mode.........................: CLIENT (requests data from an ANT Server)" << std::endl);

      APPLICATION_STATUS("Statistical history depth..............: last " << program_options.max_stats_history << " iterations" << std::endl);

      APPLICATION_STATUS("Reserved iterations depth..............: " << program_options.max_buffer_depth << std::endl);

      ANT::ANT_client ant_client;

      ant_client.run();

      APPLICATION_DEBUG_STATUS("ANT Client finished" << std::endl);
    }

  }
  catch (std::exception& e)
  {
    APPLICATION_STATUS(e.what() << std::endl);
  }
  catch (...)  //< Backup provision in case something very unusual happens
  {
    APPLICATION_STATUS("Unexpected critical error" << std::endl);
  }

  return 0;
}

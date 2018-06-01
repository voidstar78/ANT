#include <ant/include/common_types.h>  //< This CPP is implementating interfaces specified in this interface.

#if defined(WIN32)
#include <windows.h>  //< System calls to query total system memory
#else
#include <unistd.h>  //< System calls to query total system memory (sysconf, or consider sysctl)
#endif

#include <ant/include/application_status.h>

namespace ANT {

size_t total_system_memory()
{
#if defined(WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  GlobalMemoryStatusEx(&status);
  return status.ullTotalPhys;
#else  // assume POSIX/Linux
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  return pages * page_size;
#endif
}

Stopwatch sw_global_program_startup;

boost::scoped_ptr< char > rapid_data( 0 );

Program_options Program_options::program_options_;  //< Allocate the Program_options singleton.

Program_options::Program_options() 
: 
  enable_server_mode(false),
  address(default_address),  
  port(default_port), 
  // port_as_str is initialized below
  internal_buffer_length(default_buffer_length),  
  enable_log_to_file(default_log_setting),
  enable_rapid_mode(default_rapid_mode_enabled),
  status_rate(default_status_rate),
  set_nodelay(true),
  max_client_connections(default_max_client_connections),
  wait_time_between_intervals(default_interval_wait_time),
  send_file_length(0),
  max_buffer_depth(default_buffer_depth),
  client_name(default_client_name),
  bytes_to_be_transmitted(default_number_of_bytes),
  number_of_repetitions(default_number_of_reps),
  max_stats_history(default_stat_history)
{ 
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << " def" << std::endl);

  // Typically constructors should not actually do anything other than instantiate the data they encapsulate.
  // Since the initializer list can't support a safe int-to-string conversion, we'll attempt to do one
  // here within the constructor.
  this->sync_port_str();
}

Program_options::Program_options(const Program_options& other_program_options) 
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << " cpy" << std::endl);
}

Program_options::~Program_options() 
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void Program_options::sync_port_str()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  try 
  {
    std::stringstream ss;
    ss << port;  //< Convert port integer into string format.

    port_as_str = ss.str();  //< STRING COPY
  }
  catch (...)
  {
    // The default port should always be convertible, so it is really hard to imagine what could
    // really cause this exception (perhaps memory corruption or some internal CPU/core error?).
    APPLICATION_STATUS("Error converting port [" << port << "] into string format.  Proceeding at risk." << std::endl);
  }

}

std::string Program_options::results_log_filename()
{
  std::stringstream ss;
  ss << "results_" << this->client_name << ".txt";  //< TODO: add timestamp to filename

  return ss.str();
}

}  //< end namespace ANT

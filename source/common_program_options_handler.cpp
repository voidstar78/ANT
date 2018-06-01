#include <ant/include/common_program_options_handler.h>  //< This CPP is implementating interfaces specified in this interface.

// C++ STL std components used by this translation unit.
#include <iostream>  //< Since program options are initialized prior to any other threads being invoked, 
                     //  we don't need an atomic utility yet.  Any errors here will explicitly be piped to std::cerr or std::cout.
#include <fstream>   //< Utility interface to provision for loading program options from an input file stream.

// boost C++ components used by this translation unit.
#include <boost/program_options.hpp>

// Includes relevant to this translation unit.
#include <ant/include/common_types.h>

namespace ANT {

static const int err_NONE(0);
static const int err_HELP_ONLY(-1);
static const int err_INVALID_PROGRAM_OPTION_VALUE(-2);

void validate_program_options()
{
  
  auto& program_options( Program_options::instance() );

  int result(err_NONE);

  // Instead of exiting/terminating on the first invalid argument, scan all of them
  // so that all problems can be mentioned instead of just one at a time.

  {
    std::size_t n( program_options.address.length() );
    if ((n < 1) || (n > 200))  //< While IPv4 is only 15 characters ("123.123.123.123"), 200 is a reasonable size to also support URLs and such
    {
      std::cerr << "Host address length is invalid" << std::endl;
      result = err_INVALID_PROGRAM_OPTION_VALUE;
    }
  }

  // TODO port ??? is it 1-65535? Some ports are reserved... 

  if (
    (program_options.internal_buffer_length < 1)
    || (program_options.internal_buffer_length > max_transfer_test_size_allowed * ((program_options.max_client_connections > 0) ? program_options.max_client_connections : 1))
  )
  {
    std::cerr << "Invalid length for Working Buffer" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (
    (program_options.max_buffer_depth > 999999)
  )
  {
    std::cerr << "Invalid max buffer depth" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }
  else if (
    (program_options.enable_server_mode == false)
    && (program_options.max_buffer_depth < 1)
    )
  {
    std::cerr << "Invalid max buffer depth (must be at least 1 in client mode)" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  // TODO log ??? it is either true or false, on/off

  if (
    (program_options.status_rate > 21)  //< status_rate is unsigned and cannot be negative
  )
  {
    std::cerr << "Invalid status rate" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  // NODELAY is either true or false (defaults to true)

  if (
    (program_options.max_client_connections > 999999)
  )
  {
    std::cerr << "Invalid number of maximum simultaneous client connections" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (
    (program_options.wait_time_between_intervals > 99999999)
  )
  {
    std::cerr << "Invalid wait time between intervals (max is 99,999,999)" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (
    (program_options.send_filename.length() > 0)
  )
  {
    if (program_options.enable_rapid_mode)
    {
      std::cerr << "Can not enable RAPID and FILE-MODE at the same time" << std::endl;
      result = err_INVALID_PROGRAM_OPTION_VALUE;
    }
    else
    {
      // Verify that the send file can be loaded, and also store down its number of bytes.
      // This allows the number of bytes to be queried one time only, instead of each time
      // for each client connection.
      std::ifstream ifs(program_options.send_filename);
      if (ifs.is_open())
      {
        std::size_t sz(0);
        try
        {
          ifs.seekg(0, ifs.end);
          sz = ifs.tellg();
          ifs.close();
        }
        catch (...)
        {
          sz = 0;
        }

        if (sz)
        {
          if (sz > max_transfer_test_size_allowed)
          {
            std::cerr << "The specified FILE-MODE transfer file is too large (ANT Clients buffer the entire transfer into memory)" << std::endl;
            result = err_INVALID_PROGRAM_OPTION_VALUE;
          }
          else
          {
            std::cerr << "FILE-MODE has been enabled (" << program_options.send_filename << ") and numbers of bytes sent will be [" << sz << " bytes]" << std::endl;
            std::cerr << "WARNING: Although the file will not be locked by ANT, it is NOT supported to modify the file during the duration of the test.  While the entire file is NOT buffered into memory, the file will be re-accessed for each iteration of each client." << std::endl;

            program_options.send_file_length = sz;
          }
        }
        else
        {
          std::cerr << "Attempt to retrieve the send file size has failed (or the size is 0)" << std::endl;
          result = err_INVALID_PROGRAM_OPTION_VALUE;
        }
      }
      else
      {
        std::cerr << "Unable to open the specified send file of [" << program_options.send_filename << "] (check path, permissions)" << std::endl;
        result = err_INVALID_PROGRAM_OPTION_VALUE;
      }
    }
  }

  if (program_options.enable_rapid_mode)
  {
    if (program_options.send_filename.length() > 0)
    {
      std::cerr << "Can not enable RAPID and FILE-MODE at the same time" << std::endl;
      result = err_INVALID_PROGRAM_OPTION_VALUE;
    }    
  }

  {
    std::size_t n = program_options.client_name.length();
    if (
      (n < 1) 
      || (n > max_client_name_length)
    )
    {
      std::cerr << "Invalid length of client name (max is " << max_client_name_length << " characters)" << std::endl;
      result = err_INVALID_PROGRAM_OPTION_VALUE;
    }
  }

  if (
    (program_options.bytes_to_be_transmitted < 1) 
    || (program_options.bytes_to_be_transmitted > max_transfer_test_size_allowed)
  )
  {
    std::cerr << "Invalid length of transfer test (max is " << max_transfer_test_size_allowed << " bytes)" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (
    (program_options.number_of_repetitions < 1) 
    || (program_options.number_of_repetitions > 999999)
  )
  {
    std::cerr << "Invalid number of repetitions" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (
    (program_options.max_stats_history < 1)
    || (program_options.max_stats_history > 999999)
  )
  {
    std::cerr << "Invalid depth of statistical history" << std::endl;
    result = err_INVALID_PROGRAM_OPTION_VALUE;
  }

  if (result != err_NONE)
  {
    exit(result);
  }

}

void initialize_program_options(const int& argc, char** const argv)
{

  using namespace boost::program_options;

  auto& program_options(Program_options::instance());

// NOTE: Remember that #define macros are preprocessor mechanisms independent of the language
//       and therefore they do not respect scope boundaries.  These are prefixed with ANT_
//       so that hopefully they deconflict with any future integrated macros.
#define ANT_COMMAND_HELP    "help"
#define ANT_COMMAND_SERVER  "server"
#define ANT_COMMAND_CONFIG  "config"
#define ANT_COMMAND_ADDRESS "address"
#define ANT_COMMAND_PORT    "port"
#define ANT_COMMAND_WORKING "working"
#define ANT_COMMAND_LOG     "log"
#define ANT_COMMAND_STATUS  "status"
#define ANT_COMMAND_NODELAY "nodelay"
#define ANT_COMMAND_RAPID   "rapid"
#define ANT_COMMAND_CLIENTS "clients"
#define ANT_COMMAND_WAIT    "wait"
#define ANT_COMMAND_FILE    "file"
#define ANT_COMMAND_DEPTH   "depth"
#define ANT_COMMAND_NAME    "name"
#define ANT_COMMAND_BYTES   "bytes"
#define ANT_COMMAND_REPS    "reps"
#define ANT_COMMAND_HISTORY "history"

  try
  {    

    options_description main_options_descriptions("ANT options");

    main_options_descriptions.add_options()

      (ANT_COMMAND_HELP    ",h",                                                                          "Show help related to ANT program options")

      (ANT_COMMAND_SERVER  ",v",                                                                          "Run in server mode (otherwise run as client if this option is omitted)")  

      (ANT_COMMAND_CONFIG  ",c", value<std::string>()    ->default_value(default_config_filename),        "Program options configuration file (these do not override command line options)")

      (ANT_COMMAND_ADDRESS ",a", value<IP_address_type>()->default_value(default_address),                "IPv4 address for data exchange")
      (ANT_COMMAND_PORT    ",p", value<IP_port_type>()   ->default_value(default_port),                   "Port on the corresponding address to use for the data transfer")

      (ANT_COMMAND_WORKING ",w", value<std::size_t>()    ->default_value(default_buffer_length),          "Working Buffer length in bytes (read and write, 1 byte to 8GB inclusive)")

      (ANT_COMMAND_LOG     ",o", value<bool>()           ->default_value(default_log_setting),            "Log the data sent/received to a file (used for validating transfers)")

      (ANT_COMMAND_STATUS  ",s", value<unsigned int>()   ->default_value(default_status_rate),            "Status update rate (0 to 20 with lower being faster, or use 21 to disable status until transfer complete)")

      (ANT_COMMAND_NODELAY ",e", value<bool>()           ->default_value(true),                           "Set NO_DELAY on(1/true) or off(0/false)")

      (ANT_COMMAND_RAPID   ",i", value<bool>()           ->default_value(default_rapid_mode_enabled),     "(Server only) Enable RAPID-MODE (sends preallocated static data, overrides FILE-MODE and/or client specified bytes)")  
      (ANT_COMMAND_CLIENTS ",l", value<unsigned int>()   ->default_value(default_max_client_connections), "(Server only) Maximum simultaneous client connections to support (1 to 999999 inclusive, 0 is unlimited)")
      (ANT_COMMAND_WAIT    ",t", value<unsigned int>()   ->default_value(default_interval_wait_time),     "(Server only) Number of milliseconds to wait in-between intervals (0 to 99,999,999 inclusive)")
      (ANT_COMMAND_FILE    ",f", value<std::string>()    ->default_value(default_send_filename),          "(Server only) Enable FILE-MODE using this specified filename (overrides the number of bytes requested from client)")      
      (ANT_COMMAND_DEPTH   ",d", value<unsigned int>()   ->default_value(default_buffer_depth),           "(Server Mode) Maximum Working Buffer depth (1 to 999999 inclusive, 0 is unlimited)  (Client Mode) Number of iterations to reserve memory for (1 to 999999 inclusive)")

      (ANT_COMMAND_NAME    ",n", value<std::string>()    ->default_value(default_client_name),            "(Client only) Name of client instance (1 to 40 characters inclusive)")
      (ANT_COMMAND_BYTES   ",b", value<std::size_t>()    ->default_value(default_number_of_bytes),        "(Client only) Number of bytes to request for transfer (1 byte to 8GB inclusive)")
      (ANT_COMMAND_REPS    ",r", value<unsigned int>()   ->default_value(default_number_of_reps),         "(Client only) Number of times to repeat the transfer (1 to 999999 inclusive)")
      (ANT_COMMAND_HISTORY ",y", value<unsigned int>()   ->default_value(default_stat_history),           "(Client only) Maximum iterations used to represent the statistical history (1 to 999999 inclusive, total is always represented at final iteration)")
    ;

    variables_map vm;
    
    store(parse_command_line(argc, argv, main_options_descriptions), vm);  //< Accept explicit command line options first
    
    store(parse_environment(main_options_descriptions, "ANT_"), vm);  //< Accept environment variables settings (used in place of any options not specified on command line)
    
    if (vm.count(ANT_COMMAND_HELP))
    {
      std::cout << main_options_descriptions << std::endl;

      std::cout << std::endl;
      std::cout << "The priority of program option settings are in the following order:\n\t(first) Command Line Arguments\n\t(second) Environment Variables (prefix ANT_)\n\t(third) Configuration File" << std::endl;
      std::cout << std::endl;
      std::cout << "If transfer log (-o) is enabled, existing transfer logs are overwritten for the number of commanded intervals.  Be advised that the specified file should not be modified during execution of ANT." << std::endl;
      std::cout << std::endl;
      std::cout << "The client name should be a simple alpha-numeric (A-Z, 0-9) sequence to identify this test run.  Since the name is part of the transfer log filename, it is suggested to avoid spaces and special characters in the name (underscores may be ok).  The name will default to your systems host name (if possible) if no explicit client name is specified." << std::endl;
      std::cout << std::endl;
      std::cout << "The client results summary is always appended." << std::endl;

      exit(err_HELP_ONLY);
    }

    if (vm.count(ANT_COMMAND_SERVER))
    {
      program_options.enable_server_mode = true;
    }

    if (vm.count(ANT_COMMAND_CONFIG) > 0)
    {
      // If a config filename is specified, then load the settings from that 
      std::string config_filename( vm[ANT_COMMAND_CONFIG].as<std::string>().c_str() );
      std::ifstream ifs( config_filename );
      if (ifs)
      {
        std::cout << "Reading program options from [" << config_filename << "]" << std::endl;
        store(parse_config_file(ifs, main_options_descriptions), vm);
      }
    }

    if (vm.count(ANT_COMMAND_ADDRESS) > 0)
    {
      program_options.address = vm[ANT_COMMAND_ADDRESS].as<IP_address_type>();
    }

    if (vm.count(ANT_COMMAND_PORT) > 0)
    { 
      program_options.port = vm[ANT_COMMAND_PORT].as<IP_port_type>();      
      program_options.sync_port_str();
    }

    if (vm.count(ANT_COMMAND_WORKING) > 0)
    {
      program_options.internal_buffer_length = vm[ANT_COMMAND_WORKING].as<std::size_t>();
    }

    if (vm.count(ANT_COMMAND_DEPTH) > 0)
    {
      program_options.max_buffer_depth = vm[ANT_COMMAND_DEPTH].as<unsigned int>();
    }

    if (vm.count(ANT_COMMAND_LOG) > 0)
    {
      program_options.enable_log_to_file = vm[ANT_COMMAND_LOG].as<bool>();
    }

    if (vm.count(ANT_COMMAND_STATUS) > 0)
    {
      program_options.status_rate = vm[ANT_COMMAND_STATUS].as<unsigned int>();
    }

    if (vm.count(ANT_COMMAND_NODELAY) > 0)
    {
      program_options.set_nodelay = vm[ANT_COMMAND_NODELAY].as<bool>();
    }

    if (vm.count(ANT_COMMAND_RAPID) > 0)
    {
      program_options.enable_rapid_mode = vm[ANT_COMMAND_RAPID].as<bool>();
    }

    if (vm.count(ANT_COMMAND_CLIENTS) >= 0)
    {
      program_options.max_client_connections = vm[ANT_COMMAND_CLIENTS].as<unsigned int>();
    }

    if (vm.count(ANT_COMMAND_WAIT) > 0)
    {
      program_options.wait_time_between_intervals = vm[ANT_COMMAND_WAIT].as<unsigned int>();
    }

    if (vm.count(ANT_COMMAND_FILE) > 0)
    {
      program_options.send_filename = vm[ANT_COMMAND_FILE].as<std::string>();
    }

    if (vm.count(ANT_COMMAND_NAME) > 0)
    {
      program_options.client_name = vm[ANT_COMMAND_NAME].as<std::string>();
    }

    if (vm.count(ANT_COMMAND_BYTES) > 0)
    {
      program_options.bytes_to_be_transmitted = vm[ANT_COMMAND_BYTES].as<std::size_t>();
    }

    if (vm.count(ANT_COMMAND_REPS) > 0)
    {
      program_options.number_of_repetitions = vm[ANT_COMMAND_REPS].as<unsigned int>();
    }

    if (vm.count(ANT_COMMAND_HISTORY) > 0)
    {
      program_options.max_stats_history = vm[ANT_COMMAND_HISTORY].as<unsigned int>();
    }
  }
  catch (const error& exception)
  {
    // This should handle exceptional things like malformed command line arguments.
    // But it won't handle things like ports that are already in use, malformed address string, etc.
    std::cerr << exception.what() << std::endl;
  }

  validate_program_options();

}  

}  //< end namespace ANT

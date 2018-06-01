#include <ant/include/application_status.h>  //< This CPP is implementating interfaces specified in this interface.

boost::mutex output_mutex;

boost::mutex* mutex_app_output( new boost::mutex() );
boost::mutex* mutex_dbg_output( mutex_app_output );

std::ostream* app_output( &std::cout );
std::ostream* dbg_output( &std::cout );

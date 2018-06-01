#ifndef APPLICATION_STATUS_H
#define APPLICATION_STATUS_H

// C++ STL std components used among the interface(s) below
#include <iostream>  //< for std::ostream

// boost C++ components used among the interface(s) below
#include <boost/thread.hpp>  //< for mutex and lock_guard

// ************************************************************************************************
// These interfaces provides the ANT implementation for general pertinent application status output 
// and general debug output.
//
// Use a macro definition to prepare an atomic output mechanism (i.e. guarantee that the entire 
// string is output in a contiguous block of text, instead of potentially intermixed with other 
// streamed output happening in other threads at the same time).  The atomic-ness might not be 
// needed at all times, but using these interfaces also provides a consistent mechanism for 
// providing program status (allowing them to be quickly re-directed as future needs arise).
// ************************************************************************************************

extern boost::mutex* mutex_app_output;
extern boost::mutex* mutex_dbg_output;
// ^^ If both application output and debug output are set to stream to the same output, then they 
//    should share the same mutex.  However, if they use different streams, then they could each 
//    have their own mutex.  These are set to the same mutex by default, but could be adjusted
//    by the application prior to any actual intense output/status.

extern std::ostream* app_output;
extern std::ostream* dbg_output;
// ^^ These are set to std::cout by default.  End user can override these to ofstreams if/as desired.

#define APPLICATION_STATUS(expression)                                                \
  {                                                                                   \
    assert(mutex_dbg_output != 0);                                                    \
    assert(app_output != 0);                                                          \
    boost::lock_guard<boost::mutex> lock(*mutex_app_output);                          \
    (*app_output) << expression;                                                      \
  }

#if defined(_DEBUG)
  #define APPLICATION_DEBUG_STATUS(expression)                                          \
    {                                                                                   \
      assert(mutex_dbg_output != 0);                                                    \
      assert(dbg_output != 0);                                                          \
      boost::lock_guard<boost::mutex> lock(*mutex_dbg_output);                          \
      (*dbg_output) << expression;                                                      \
    }
#else
  // In a NON-DEBUG (i.e. RELEASE) build, the APPLICATION_DEBUG_STATUS is disabled.
  #define APPLICATION_DEBUG_STATUS(expression)                                          \
    {                                                                                   \
    }
#endif

#endif

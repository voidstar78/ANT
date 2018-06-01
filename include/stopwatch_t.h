#ifndef STOPWATCH_T_H
#define STOPWATCH_T_H

#include <cstdint>                            // used for uint64_t type
#include <boost/chrono.hpp>                   // high resolution timer (note this brings in boost::unique_ptr also! boost 1.58)

namespace ANT {

template<class T = std::uint64_t>
class Stopwatch_t  //< A quick processing-time stopwatch utility (auto-starts on instance construction)
{

public:

  typedef T value_type;

  Stopwatch_t() : start_time_(boost::chrono::high_resolution_clock::now()) { }

  inline void restart() { start_time_ = boost::chrono::high_resolution_clock::now(); }

  inline T elapsed_ns() const
  { 
    boost::chrono::nanoseconds nano_s( boost::chrono::duration_cast<boost::chrono::nanoseconds>(boost::chrono::high_resolution_clock::now() - start_time_) );
    return static_cast<T>(nano_s.count());
  }

  inline T elapsed_ms() const
  {
    boost::chrono::milliseconds milli_s( boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::high_resolution_clock::now() - start_time_) );
    return static_cast<T>(milli_s.count());
  }

private:

  boost::chrono::high_resolution_clock::time_point start_time_;  // TODO: Is this already atomic, or does it need to be wrapped into a boost::atomic<>?

};

}  // end namespace ANT

#endif

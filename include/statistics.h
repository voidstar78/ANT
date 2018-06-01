#ifndef ANT_STATISTICS_H
#define ANT_STATISTICS_H

#include <cstdint>  //< for the uint64_t data type
#include <ostream>

namespace ANT {

//TODO: While formal mutators ought to be defined, this is an internal-only statistics "bucket" specifically for ANT.
//      No re-use is intended or expected.
struct Statistics
{

  struct Performance_datum
  {
    uint64_t data_send_time_;
    uint64_t bytes_sent_;
    uint64_t copy_to_application_time_;
    uint64_t data_read_time_;

    Performance_datum();
    Performance_datum(const Performance_datum& other_pd);

    ~Performance_datum();    

    void operator+=(const Performance_datum& other_pd);
    void operator-=(const Performance_datum& other_pd);
  };

  unsigned int current_iteration_;
  unsigned int total_iterations_;
  Performance_datum current_working_performance_datum_;  

  void accumulate_current_working_datum();
  void summarize_end_of_iteration_status(size_t summarize_end_of_iteration_status);
  void stream_and_clear_current_summary_results(std::ostream& ofs);

  static Statistics& instance();

private:

  Statistics();

  struct Impl;
  Impl* pimpl_;
  Impl& impl_;

};

}  // end namespace ANT

#endif

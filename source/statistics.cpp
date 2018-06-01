#include <ant/include/statistics.h>  //< This CPP is implementating interfaces specified in this interface.

// C++ STL std components
#include <string>
#include <vector>

// boost C++ components used by this translation unit.
#include <boost/format.hpp>
#include <boost/circular_buffer.hpp>

// Application-specific includes relevant to this translation unit.
#include <ant/include/application_status.h>
#include <ant/include/common_types.h>

namespace {  //< Implicit local namespace

template<class T>
std::string add_commas(const T& other_str)
{
  std::string result(other_str.str());  // COPY

  auto period_pos(result.find("."));
  if (period_pos != std::string::npos)
  {
    while (period_pos > 3)
    {
      period_pos -= 3;
      result.insert(period_pos, 1, ',');
    }
  }

  return result;
}

template<class A, class B>
double to_double_ratio(const A& a, const B& b)
{
  double result( static_cast<double>(a) / static_cast<double>(b) );
  return result;
}

}

namespace ANT {

struct Statistics::Impl
{
  
  Impl()
  {
    APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

    Program_options& program_options(Program_options::instance());

    //performance_data_.resize(program_options.max_stats_history);
    performance_data_.set_capacity(program_options.max_stats_history);
  }

  ~Impl()
  {
    APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
  }

  typedef boost::circular_buffer< Statistics::Performance_datum > Performance_data;
  Performance_data performance_data_;

  typedef std::vector<std::string> Summary_results;
  Summary_results summary_results_;

  Statistics::Performance_datum total_working_performance_datum_;

  Performance_data::value_type totals_from_last_few_;

};
  
Statistics::Performance_datum::Performance_datum() 
:
  data_send_time_(0),
  bytes_sent_(0),
  copy_to_application_time_(0),
  data_read_time_(0)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << " def " << std::endl);
}

Statistics::Performance_datum::Performance_datum(const Performance_datum& other_pd)
:
  data_send_time_(other_pd.data_send_time_),
  bytes_sent_(other_pd.bytes_sent_),
  copy_to_application_time_(other_pd.copy_to_application_time_),
  data_read_time_(other_pd.data_read_time_)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << " cpy " << std::endl);
}

Statistics::Performance_datum::~Performance_datum()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void Statistics::Performance_datum::operator+=(const Performance_datum& other_pd)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  this->data_send_time_ += other_pd.data_send_time_;
  this->bytes_sent_ += other_pd.bytes_sent_;
  this->copy_to_application_time_ += other_pd.copy_to_application_time_;
  this->data_read_time_ += other_pd.data_read_time_;
}

void Statistics::Performance_datum::operator-=(const Performance_datum& other_pd)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  this->data_send_time_ -= other_pd.data_send_time_;
  this->bytes_sent_ -= other_pd.bytes_sent_;
  this->copy_to_application_time_ -= other_pd.copy_to_application_time_;
  this->data_read_time_ -= other_pd.data_read_time_;
}

Statistics& Statistics::instance()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  static ANT::Statistics statistics_;
  return statistics_;
}

Statistics::Statistics()
:
  current_iteration_(0),
  total_iterations_(0),
  // --------------------------
  pimpl_(
    new Impl()
  ),
  impl_(*pimpl_)
{
}

void Statistics::accumulate_current_working_datum()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (impl_.performance_data_.size() == impl_.performance_data_.capacity())
  {
    // Now that the circular buffer is full, pop-off the front data from the
    // rolling average (and the next/new entry is pushed on below)
    impl_.totals_from_last_few_ -= *(impl_.performance_data_.begin());  // BEGIN is about to get pushed off due to the push_back below
  }

  impl_.performance_data_.push_back(current_working_performance_datum_);

  impl_.totals_from_last_few_ += current_working_performance_datum_;  //< If the circular buffer holding the statistics got full, the first/earliest store stats were already popped off above.

  impl_.total_working_performance_datum_ += current_working_performance_datum_;  //< Accumulate the total of all the runs
}

void Statistics::summarize_end_of_iteration_status(size_t long_buffer_length)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);  

  static const double ns_to_s( 1.0 / static_cast<double>(1000000000.0) );
  double avg_xmit_time( to_double_ratio(impl_.totals_from_last_few_.data_send_time_, impl_.performance_data_.size()) * ns_to_s );
  double avg_recv_time( to_double_ratio(impl_.totals_from_last_few_.data_read_time_, impl_.performance_data_.size()) * ns_to_s );
  double avg_copy_time( to_double_ratio(impl_.totals_from_last_few_.copy_to_application_time_, impl_.performance_data_.size()) * ns_to_s );

  static const double b_to_mb( 1.0 / static_cast<double>(1024.0 * 1024.0) );
  double avg_mbytes_sent( to_double_ratio(impl_.totals_from_last_few_.bytes_sent_, impl_.performance_data_.size()) * b_to_mb );
  
  double avg_xmit_rate( avg_mbytes_sent / avg_xmit_time );
  double avg_recv_rate( avg_mbytes_sent / (avg_recv_time + avg_copy_time));
  double memory_xfer_rate( avg_mbytes_sent / avg_copy_time );
  
  {
    std::stringstream ss;
    ss << 
      "(" << (boost::format("%06u") % this->current_iteration_) << "/" << (boost::format("%06u") % this->total_iterations_) << ") " <<
      "[memory=" << add_commas(boost::format("%010.3f") % memory_xfer_rate) << "  " <<
      "SEND=" << add_commas(boost::format("%010.3f") % avg_xmit_rate) << "  " <<
      "RECV=" << add_commas(boost::format("%010.3f") % avg_recv_rate) << " MBps]  " <<
      "[SEND=" << add_commas(boost::format("%013.6f") % (avg_mbytes_sent/avg_xmit_rate)) << "  " <<
      "RECV=" << add_commas(boost::format("%013.6f") % (avg_mbytes_sent/avg_recv_rate)) << " sec] (" <<
      long_buffer_length << ")";

    impl_.summary_results_.push_back(ss.str());
    APPLICATION_STATUS(*(impl_.summary_results_.rbegin()) << std::endl);
  }

  if (current_iteration_ == total_iterations_)
  {
    double avg_xmit_time( to_double_ratio(impl_.total_working_performance_datum_.data_send_time_, total_iterations_) * ns_to_s );
    double avg_recv_time( to_double_ratio(impl_.total_working_performance_datum_.data_read_time_, total_iterations_) * ns_to_s ); 
    double avg_copy_time( to_double_ratio(impl_.total_working_performance_datum_.copy_to_application_time_, total_iterations_) * ns_to_s );

    double avg_mbytes_sent( to_double_ratio(impl_.total_working_performance_datum_.bytes_sent_, total_iterations_) * b_to_mb );
  
    double avg_xmit_rate( avg_mbytes_sent / avg_xmit_time );
    double avg_recv_rate( avg_mbytes_sent / (avg_recv_time + avg_copy_time));
    double memory_xfer_rate( avg_mbytes_sent / avg_copy_time );

    std::stringstream ss;
    ss << 
      "(    TOTAL    ) " <<
      "[memory=" << add_commas(boost::format("%010.3f") % memory_xfer_rate) << "  " <<
      "SEND=" << add_commas(boost::format("%010.3f") % avg_xmit_rate) << "  " <<
      "RECV=" << add_commas(boost::format("%010.3f") % avg_recv_rate) << " MBps]  " <<
      "[SEND=" << add_commas(boost::format("%013.6f") % (avg_mbytes_sent/avg_xmit_rate)) << "  " <<
      "RECV=" << add_commas(boost::format("%013.6f") % (avg_mbytes_sent/avg_recv_rate)) << " sec] (TOTAL) (" <<
      long_buffer_length << ")";

    impl_.summary_results_.push_back(ss.str());
    APPLICATION_STATUS(*(impl_.summary_results_.rbegin()) << std::endl);
  }
}

void Statistics::stream_and_clear_current_summary_results(std::ostream& ofs)
{
  auto ssr_iter = impl_.summary_results_.begin();
  while (ssr_iter != impl_.summary_results_.end())
  {
    ofs << (*ssr_iter) << std::endl;

    ++ssr_iter;
  }
  impl_.summary_results_.clear();
}

}

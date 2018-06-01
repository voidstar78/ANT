#include <ant/include/ant_server.h>  //< This CPP is implementating interfaces specified in this interface.

// C++ STL std components
#include <fstream>                            // file output handling via ofstream

// boost C++ components 
#define BOOST_SP_USE_QUICK_ALLOCATOR  // potential performance improvement mentioned in The Boost C++ Libraries 2nd Edition pg8
#include <boost/make_shared.hpp>
#include <boost/algorithm/string.hpp>         // provides split and other string utilities
#include <boost/random.hpp>                   // mt19937, etc random number generators
#include <boost/thread.hpp>                   // provides thread and mutex support
#include <boost/atomic.hpp>                   // provide atomic integer accounting when shared across threads
#include <boost/scoped_ptr.hpp>               // not sure if should use unique_ptr instead...
#include <boost/thread/scoped_thread.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/asio.hpp>  
#include <boost/asio/steady_timer.hpp> 
#include <boost/chrono.hpp>

// Application-specific includes relevant to this translation unit.
#include <ant/include/common_types.h>
#include <ant/include/application_status.h>
#include <ant/include/client_connection_manager.h>

namespace ANT {

struct ANT_server::Impl
{

  // TYPES
  struct Client_connection_datum
  {

    static const Connection_ID_type uninitialized_client_id = -1;

    Connection_ID_type connection_id;

    uint64_t creation_offset_time;

    typedef boost::shared_ptr<boost::asio::io_service> Client_io_service_SP;
    Client_io_service_SP client_io_service;

    Client_connection_manager::Client_connection_manager_SP actual_client;

  private:

    Client_connection_datum() 
    : 
      connection_id(uninitialized_client_id),
      creation_offset_time(0),
      client_io_service(0) 
    { 
      APPLICATION_DEBUG_STATUS("CID" << connection_id << ": " << __FUNCTION__ << " def @ " << this << std::endl);
      assert(false && "Why is this default constructor being called?");      
    }

  public:

    Client_connection_datum(Connection_ID_type a_connection_id) 
    : 
      connection_id(a_connection_id),
      creation_offset_time(0),
      client_io_service(new Client_io_service_SP::element_type()) 
    { 
      APPLICATION_DEBUG_STATUS("CID" << connection_id << ": " << __FUNCTION__ << " usr @ " << this << std::endl);
    }

    Client_connection_datum(const Client_connection_datum& other_ccd) 
    : 
      connection_id(other_ccd.connection_id),
      creation_offset_time(other_ccd.creation_offset_time),
      client_io_service(other_ccd.client_io_service),
      actual_client(other_ccd.actual_client)
    { 
      APPLICATION_DEBUG_STATUS("CID" << connection_id << ": " << __FUNCTION__ << " cpy @ " << this << std::endl);
    }

    ~Client_connection_datum()
    {
      APPLICATION_DEBUG_STATUS("CID" << connection_id << ": " << __FUNCTION__ << " bb @ " << this << std::endl);
    }
   
  };
  typedef std::map<Connection_ID_type, Client_connection_datum> Client_connection_data; 

  // IMPLEMENTATION INTERFACES
  Impl();
  ~Impl();

  void prepare_to_accept_next_connection();

  void perform_client_processing(Client_connection_manager::Client_connection_manager_SP new_connection);

  void handle_terminate(const boost::system::error_code& error, int signal_number);

  void handle_accept(Client_connection_manager::Client_connection_manager_SP new_connection, const boost::system::error_code& error);

  void signal_connection_complete(Connection_ID_type connection_id);  //const Client_connection_datum& a_client_connection_datum);

  inline static boost::asio::io_service& application_io_service() 
  { 
    static boost::asio::io_service io_service; 
    return io_service; 
  }

  // DATA
  boost::mutex mutex_client_connection_data_;
  Client_connection_data client_connection_data_; 

  boost::atomic<bool> client_capacity_is_past_full_;

  boost::asio::ip::tcp::acceptor tcp_acceptor_;

  Program_options& program_options_;

  boost::asio::signal_set signals_;

};

ANT_server::Impl::Impl()
:
  client_capacity_is_past_full_(false),

  tcp_acceptor_(

    application_io_service(),

    boost::asio::ip::tcp::endpoint(
      boost::asio::ip::address::from_string( Program_options::instance().address ),
      Program_options::instance().port
    )

  ),
  program_options_(Program_options::instance()),
  signals_(application_io_service())
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
  
  // ********************* re: Nagle Algorithm
  boost::asio::ip::tcp::no_delay opt_nodelay(program_options_.set_nodelay);
  tcp_acceptor_.set_option(opt_nodelay);
  // *********************

  {
    auto& ep = tcp_acceptor_.local_endpoint();
    APPLICATION_STATUS("Accepting client connection(s) on " << ep.address().to_string() << ":" << ep.port() << std::endl);
  }
  
  signals_.add(SIGINT);
  signals_.add(SIGTERM);
  signals_.async_wait(
    boost::bind(  
      &ANT_server::Impl::handle_terminate,
      // --------------------------------------------
      this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::signal_number
    )
  );
}

ANT_server::Impl::~Impl()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void ANT_server::Impl::prepare_to_accept_next_connection()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  // Queue up and prepare for the first or next client connection... (this is a shared pointer since we support multiple client connections at the same time)
  Client_connection_manager::Client_connection_manager_SP new_connection(0);
  do
  {
    static const Connection_ID_type initial_connection_id( 0 );
    static boost::atomic<Connection_ID_type> next_connection_id(initial_connection_id);  //< This is atomic since multiple clients might try connecting at the same time

    ++next_connection_id;                                                 // INCREMENT (do this first in case multiple clients are attempting to connect at the same time)
    Connection_ID_type assigned_connection_id_copy = next_connection_id;  // COPY

    static const Connection_ID_type max_connection_id_rollover = 999999;  
    // ^^^ This just caps the ConnectionID to reasonable numbers and avoids them rolling to negative if the type is or becomes signed.
    //     However, this does create a possibility that a client that lingers for a very long time will get its ID assigned again.
    //     This is checked for in the manager processing and the (new) client will get cancelled and asked to connect again.

    if (next_connection_id > max_connection_id_rollover)
    {
      APPLICATION_STATUS("ConnectionID rollover occurred!" << std::endl);
      next_connection_id = initial_connection_id;
    }

    // Verify that the ConnectionID hasn't already been assigned to another client instance, due to rollover
    bool ok_to_create = false;
    {
      boost::lock_guard<boost::mutex> locked_client_connection_data_(this->mutex_client_connection_data_);             
      auto ccd_iter = client_connection_data_.find( assigned_connection_id_copy );  // COPY ITERATOR (TODO: Should this be auto&?)
      if (ccd_iter == client_connection_data_.end())
      {
        // This assigned ConnectionID is already in use.  Use the while-loop to try again.
        ok_to_create = true;
      }
    }
    if (ok_to_create)
    {
      new_connection = Client_connection_manager::create( assigned_connection_id_copy );

      // Done defining the new ConnectionID, proceed with preparing the connection
      break;
    }    
  } while (true);

  if (new_connection)  // sanity-check defensive programming
  {

    // Queue an asyncronous activity that is invoked during the next accepted client connection (boost/OS handles the H/W exchanges used to establish the connection)
    tcp_acceptor_.async_accept(   
      new_connection->socket(),
      boost::bind(  //< The usage of bind here will cause the new_connection use_count to increment from 1 to 2 (so the client manager instance is preserved past this method call)
        &ANT_server::Impl::handle_accept,  //< Invoked when the next client connection is accepted
        // --------------------------------------------
        this,        
        new_connection,
        boost::asio::placeholders::error
      )
    );

  }
  else
  {
    APPLICATION_STATUS("Error allocating for next connection" << std::endl);
  }
}

void ANT_server::Impl::perform_client_processing(Client_connection_manager::Client_connection_manager_SP new_connection)
{
  if (new_connection)
  {
    if (client_capacity_is_past_full_)
    {
      APPLICATION_STATUS("Issuing an explicit closure of CID" << new_connection->connection_id() << " - client capacity has been exceeded" << std::endl);
      // Do nothing for this client (past_full flag is cleared after a legit client finishes)
      new_connection->socket().close();
      return;
    }
    else
    {
      new_connection->start();
    }
  }
}

void ANT_server::Impl::handle_terminate(const boost::system::error_code& error, int signal_number)
{
  if (error)
  {
    APPLICATION_STATUS("Error [" << error << "] while attempting to handle termination signal(s)" << std::endl);
  }
  else
  {

try_again:
    {
      boost::lock_guard<boost::mutex> locked_client_connection_data_(this->mutex_client_connection_data_);

      APPLICATION_STATUS("Attempting to stop " << this->client_connection_data_.size() << " client(s)" << std::endl);

      auto ccd_iter = this->client_connection_data_.begin();
      while (ccd_iter != this->client_connection_data_.end())
      {
        if (ccd_iter->second.actual_client)  //< Sanity check on the pointer
        {
          ccd_iter->second.actual_client->explicit_stop();
        }
        ++ccd_iter;
      }
    }

    // Give some time for all the clients to close...  The explicit_stop should
    // set a flag to indicate to stop all processing, which we can't anticipate where
    // in the processing the client might be (could be writing to log, issuing another
    // iteration, mid-iteration, etc).  Allow time for the clients to terminate 
    // gracefully with the explicit_stop flag having been set.
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));

    // Having given the clients time to stop, when they stop they should issue a termination
    // signal that removes them from the client_connection_data_.  If all the clients are
    // properly stopped, the size of this collection should now be 0.  Check this
    // condition, and if false, then try again to terminate remaining clients (or give
    // them more time to close).
    {
      boost::lock_guard<boost::mutex> locked_client_connection_data_(this->mutex_client_connection_data_);

      if (this->client_connection_data_.size() == 0)
      {
        APPLICATION_STATUS("All clients terminated" << std::endl);

        this->application_io_service().stop();
      }
      else
      {
        goto try_again;
      }
    }
  }
}

void ANT_server::Impl::handle_accept(
  Client_connection_manager::Client_connection_manager_SP new_connection,
  const boost::system::error_code& error
)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (error)
  {
    // server not available? NIC broken/unplugged/disabled mid-connection?
    APPLICATION_STATUS("CID" << ((new_connection.get() == 0) ? -1 : new_connection->connection_id()) << ": Error during accepting the connection (" << error << ")" << std::endl);
  }
  else
  {
    if (new_connection == 0)  //< Defensive programming, no real reason this should ever fail.
    {
      APPLICATION_STATUS("Failed allocation while attempting to accept a connection" << std::endl);
    }
    else
    { 
      // Begin ANT application for this client connection!

      boost::asio::ip::tcp::no_delay opt_nodelay(program_options_.set_nodelay);
      new_connection->socket().set_option(opt_nodelay);

      const auto& local_endpoint( new_connection->socket().local_endpoint() );
      APPLICATION_STATUS("CID" << new_connection->connection_id() << ": Accepting client connection from [" << local_endpoint.address().to_string() << ":" << local_endpoint.port() << "]" << std::endl);   

      {
        // The connectionID does not exist already in our local connection data cache.  Create a new entry
        // corresponding to this connectionID.
        auto client_id_copy( new_connection->connection_id() );

        Client_connection_datum* a_client_connection(0);
        {
          boost::lock_guard<boost::mutex> locked_client_connection_data_(this->mutex_client_connection_data_);             
          auto& cc_data_pair(
            client_connection_data_.emplace( 
              std::make_pair(
                client_id_copy, 
                Client_connection_datum(client_id_copy)  
              ) 
            )
          );
          if (cc_data_pair.second)  //< Verify that the insertion actually worked...
          {            
            a_client_connection = &(cc_data_pair.first->second);
            
            if (
              (program_options_.max_client_connections > 0)
              )
            {
              APPLICATION_STATUS("Attempting to accept new client.  Number of current client entries is " << client_connection_data_.size() << std::endl);
              if (client_connection_data_.size() > program_options_.max_client_connections)
              {
                APPLICATION_DEBUG_STATUS(">>>> CLIENT CAPACITY HAS BEEN EXCEEDED, NO NEW CLIENTS ACCEPTED UNTIL A CURRENT ONE DISCONNECTS <<<<" << std::endl);
                client_capacity_is_past_full_ = true;
              }
            }
            else
            {
              // Unlimited client connections (limited only by the capacity of the server/OS/HW)
            }
          }
        }

        prepare_to_accept_next_connection();

        if (a_client_connection)
        {

          a_client_connection->creation_offset_time = sw_global_program_startup.elapsed_ns();

          a_client_connection->actual_client = new_connection;

          a_client_connection->client_io_service->dispatch(  
            boost::bind(
              &ANT_server::Impl::perform_client_processing,
              this,
              new_connection
            )  
          ); 

          // Keep the connection alive by using a per-client thread...
          boost::thread thread(
            [&, new_connection, this]()
            { 
              a_client_connection->client_io_service->run();  // run the async functions queued for this client
              // NOTE: The intent here is that the client connection manager start() method runs in a thread,
              // and that instance is kept alive until this thead ends.   We want the clients to be able to
              // be executed in parallel and not sequentially.

              this->signal_connection_complete(new_connection->connection_id());
            } 
          );

        }
      }
    }
  }
}

void ANT_server::Impl::signal_connection_complete(Connection_ID_type connection_id)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  {      
    boost::lock_guard<boost::mutex> locked_client_connection_data_(this->mutex_client_connection_data_);    

    auto ccd_iter = client_connection_data_.find(connection_id);
    if (ccd_iter == client_connection_data_.end())
    {
      // How did this happen?  Book-keeping error, someone adjusted the code that caused
      // this connection_id to already get removed from the client connection data map.
      APPLICATION_DEBUG_STATUS("CID" << connection_id << " disappeared!!! <<-------------------" << std::endl);
    }
    else
    {
      auto client_end_time = sw_global_program_startup.elapsed_ns(); 
      auto client_existence_time_delta = client_end_time - ccd_iter->second.creation_offset_time;
      APPLICATION_STATUS("Done serving client connection [" << connection_id << "] which was serviced for [" << client_existence_time_delta << "ns]" << std::endl);

      client_connection_data_.erase(ccd_iter);  // this erases this key entry, which in turn de-allocate the corresponding datum and connection shared pointer(s)

      APPLICATION_DEBUG_STATUS("CID" << connection_id << " record has been removed" << std::endl);
    }

    if (
      this->client_capacity_is_past_full_ 
      && (client_connection_data_.size() < program_options_.max_client_connections)
    )
    {
      APPLICATION_DEBUG_STATUS("Clearing ClientCapacityPastFull flag (slot now open for another new client connection)" << std::endl);
      this->client_capacity_is_past_full_ = false;
    }
  }
}

ANT_server::ANT_server()
:
  pimpl_(
    new Impl()
  ),
  impl_(*pimpl_)
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

ANT_server::~ANT_server()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);
}

void ANT_server::run()
{
  APPLICATION_DEBUG_STATUS(__FUNCTION__ << std::endl);

  if (impl_.program_options_.enable_rapid_mode)
  {
    APPLICATION_STATUS("Initializing RAPID-DATA (one time only, " << impl_.program_options_.internal_buffer_length << " bytes)" << std::endl);
    if (impl_.program_options_.max_buffer_depth != 0)
    {
      APPLICATION_STATUS("Overriding buffer depth to 0 (unlimited) due to RAPID-DATA mode" << std::endl);
      impl_.program_options_.max_buffer_depth = 0;
    }

    // In rapid-mode, each iteration of each client re-uses this same rapid_data allocation.
    // It is allocated once here, then never de-allocated until the server ends.
    rapid_data.reset( new char[impl_.program_options_.internal_buffer_length] );
    if (rapid_data)
    {
      auto* raw_rapid_data = rapid_data.get();
      // We can safely assume that the new-allocation is contiguous memory from the application/process
      // perspective.  Within the OS, the memory could possibly be virtual and fragmented.
      memset(raw_rapid_data, static_cast<int>('A'), impl_.program_options_.internal_buffer_length);
    }
  }

  impl_.prepare_to_accept_next_connection();  //< Start accepting new connections

  impl_.application_io_service().run();
  // ^^ Normally the run() will execute indefinately as long as there are client slots available to be connected.
}

}

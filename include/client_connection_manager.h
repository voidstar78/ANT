#ifndef CLIENT_CONNECTION_MANAGER_H
#define CLIENT_CONNECTION_MANAGER_H

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>

#include <ant/include/common_types.h>  //< Necessary for the Connection_ID_type

namespace ANT {

class Client_connection_manager
{

public:

  typedef boost::shared_ptr<Client_connection_manager> Client_connection_manager_SP;  
  
  static Client_connection_manager_SP create( Connection_ID_type assigned_connection_id );

  void start();
  void explicit_stop();

  Connection_ID_type connection_id() const; 

  const boost::asio::ip::tcp::socket& socket() const;
  boost::asio::ip::tcp::socket& socket();

  ~Client_connection_manager();  //< shared_ptr usage of this class requires the destructor to be public

private:  

  Client_connection_manager(Connection_ID_type assigned_connection_id);
  Client_connection_manager(const Client_connection_manager& other_ccm); 

  struct Impl;
  Impl* pimpl_;
  Impl& impl_;

};

}

#endif

#ifndef ANT_SERVER_H
#define ANT_SERVER_H

namespace ANT {

class ANT_server
{

public:

  ANT_server();
  ~ANT_server();

  void run();

private:

  struct Impl;
  Impl* pimpl_;
  Impl& impl_;

};

}

#endif

#ifndef ANT_CLIENT_H
#define ANT_CLIENT_H

namespace ANT {

class ANT_client
{

public:

  ANT_client();
  ~ANT_client();

  void run();

private:

  struct Impl;
  Impl* pimpl_;
  Impl& impl_;

};

}  //< end namespace ANT

#endif

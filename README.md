# ANT
A Network (Performance) Tester [C++]

Beyond just being another Network Performance tester (like LST or iperf), ANT provides a reference
framework for how to efficiently share unstructured data between two processes via TCP/IP.

While there are other solutions to this kind of data sharing (such as ActiveMQ),  ANT has a few unique
and useful aspects.  The main features include being cross-platform via use of boost, is a completely native C++ solution,
and is highly runtime efficient by using lambda expressions to invoke various processing across threads/cores.

ANT is also a specific client/server protocol.  The ANT reference application that uses its own protocol happens
to be a network performance analysis tool.  But this approach can be expanded to provide broader
utility, such as a DTED/terrain lookup provider.  i.e. new commands can be added to request data of a 
specific type or format.

This protocol allows transfer of both ASCII and binary payloads.  It uses a HEADER/FOOTER instrumented 
with timing metrics to assist with monitoring performance.  The implementation handles multiple client connection, 
is well behaved (gracefully handles unexpected server or client disconnects), and accounts for handing of 
received data streams to the application for its exclusive usage.  i.e. it doesn't just count bytes then toss them.

See the "docs" folder for additional details.

Contact me if you would like notes on how I adapted ANT into a Terrain Server.

** BOUNTY OPPORTUNITIES **

I'm willing to pay an incentive for the following:
- Finding of any meaningful performance improvements within the implementation (e.g. not just compiler optimization or parsing command line
arguments more efficiently, but actually design or data structure changes to make data send/recv performance faster)
- Finding of any logic error, particularly within the statistics accounting
- Duplication of this capability in Java (including multi-client support, threaded async sends, nanosecond
precision accounting, FILE and RAPID/NON-RAPID modes, and hand-off of receives data to the client appliction)
- Preparation of gcc build scripts


# TCP-Proxy
School Project

> Works

The programs handle both the reconnection aspect of the same session, and the new session. The heartbeat aspect of the program seems to be functioning correctly as well. The two proxy programs survive host mobility. Though there are some issues in regards to how cproxy is handling the data coming from sproxy. Some unexpected 0 might show up and some ping lines are not printed

> Not Working

The reliable transfer aspect of the programs is not working, the implementation of the queue was not working correctly. In order to ensure that the program compiled and satisfied the other aspects of the project, the queue was not implemented into the proxy programs, leaving the reliable transfer aspect unimplemented.

The issue unsolved is how to update seq and ack and communicate it between cproxy and sproxy. We can almost log into the telnet but we don’t have time to debug it anymore. I keep our reliable transfer part code inside a separate folder for your review.

For seq and ack, I also tried to implement wrap-around. So they can’t go higher than
9999 since there are only 4 char saved for each of them.

Note that there might remain some debugging code I haven’t commented out.

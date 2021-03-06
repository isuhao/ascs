ascs (a successor of st_asio_wrapper)
===============
Overview
-
ascs is an asynchronous c/s framework based on Asio(non-Boost), besides all benefits brought by Asio, it also contains: </br>
1. Based on message just like UDP with several couple of build-in packer and unpacker;</br>
2. Support packer and unpacker customization, and replacing packer and unpacker at run-time;</br>
3. Automatically reconnect to the server after link broken;</br>
4. Widely support timers;</br>
5. Support object pool, object reusing;</br>
6. Support message buffer;</br>
7. Support ssl;</br>
8. Support TCP/UDP.</br>
Quick start:
-
###server:
Derive your own socket from `server_socket_base`, you must at least re-write the `on_msg` or `on_msg_handle` virtual function and handle messages in it;</br>
Create a `service_pump` object, create a `server_base` object, call `service_pump::start_service`;</br>
Call `server_socket_base::send_msg` when you have messages need to send.</br>
###client:
Derive your own socket from `connector_base`, you must at least re-write the `on_msg` or `on_msg_handle` virtual function and handle messages in it;</br>
Create a `service_pump` object, create a `tcp::client` object, set server address via `connector_base::set_server_addr`, call `service_pump::start_service`;</br>
Call `connector_base::send_msg` when you have messages need to send.</br>
Directory structure:
-
All source codes are placed in directory `include/ascs/`, demos are placed in directory `examples`, documents are placed in directory `doc`.</br>
Demos:
-
###echo_server:
Demonstrate how to implement tcp servers, it cantains two servers, one is the simplest server (normal server), which just send characters from keyboard to all clients (from `client` demo), and receive messages from all clients (from `client` demo), then display them; the other is echo server, which send every received message from `echo_client` demo back.</br>
###client:
Demonstrate how to implement tcp client, it simply send characters from keyboard to normal server in `echo_server`, and receive messages from normal server in `echo_server`, then display them.</br>
###echo_client:
Used to test `ascs`'s performance (whith `echo server`).</br>
###file_server:
A file transfer server.</br>
###file_client:
A file transfer client, use `get <file name1> [file name2] [...]` to fetch files from `file_server`.</br>
###udp_client:
Demonstrate how to implement UDP communication.</br>
###ssl_test:
Demonstrate how to implement TCP communication with ssl.</br>
Compiler requirement:
-
Visual C++ 10.0, GCC 4.6 or Clang 3.1 at least, with c++14 features;</br>
Debug environment:
-
win10 vc2015 32/64 bit</br>
Debian 8 32/64 bit</br>
Ubuntu 16 64 bit</br>
Fedora 23 64 bit</br>
FreeBSD 10 32/64 bit</br>
email: mail2tao@163.com
-
Community on QQ: 198941541
-

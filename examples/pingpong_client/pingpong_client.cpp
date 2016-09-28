
#include <iostream>

//configuration
#define ASCS_SERVER_PORT		9527
#define ASCS_REUSE_OBJECT //use objects pool
//#define ASCS_FORCE_TO_USE_MSG_RECV_BUFFER
//#define ASCS_WANT_MSG_SEND_NOTIFY
#define ASCS_MSG_BUFFER_SIZE 65536
#define ASCS_DEFAULT_UNPACKER stream_unpacker //non-protocol
//configuration

#include <ascs/ext/tcp.h>
using namespace ascs;
using namespace ascs::tcp;
using namespace ascs::ext;
using namespace ascs::ext::tcp;

#ifdef _MSC_VER
#define atoll _atoi64
#endif

#define QUIT_COMMAND	"quit"
#define LIST_STATUS		"status"

ascs_cpu_timer begin_time;
std::atomic_ushort completed_session_num;

class echo_socket : public connector
{
public:
	echo_socket(asio::io_service& io_service_) : connector(io_service_) {}

	void begin(size_t msg_num, const char* msg, size_t msg_len)
	{
		total_bytes = msg_len;
		total_bytes *= msg_num;
		send_bytes = recv_bytes = 0;

		send_native_msg(msg, msg_len);
	}

protected:
	virtual void on_connect() {asio::ip::tcp::no_delay option(true); lowest_layer().set_option(option); connector::on_connect();}

	//msg handling
#ifndef ASCS_FORCE_TO_USE_MSG_RECV_BUFFER
	virtual bool on_msg(out_msg_type& msg) {handle_msg(msg); return true;}
#endif
	virtual bool on_msg_handle(out_msg_type& msg, bool link_down) {handle_msg(msg); return true;}

#ifdef ASCS_WANT_MSG_SEND_NOTIFY
	virtual void on_msg_send(in_msg_type& msg)
	{
		send_bytes += msg.size();
		if (send_bytes < total_bytes)
			direct_send_msg(std::move(msg));
	}
#endif

private:
#ifdef ASCS_WANT_MSG_SEND_NOTIFY
	void handle_msg(out_msg_ctype& msg)
	{
		recv_bytes += msg.size();
		if (recv_bytes >= total_bytes && 0 == --completed_session_num)
			begin_time.stop();
	}
#else
	void handle_msg(out_msg_type& msg)
	{
		if (0 == total_bytes)
			return;

		recv_bytes += msg.size();
		if (recv_bytes >= total_bytes)
		{
			total_bytes = 0;
			if (0 == --completed_session_num)
				begin_time.stop();
		}
		else
			direct_send_msg(std::move(msg));
	}
#endif

private:
	uint64_t total_bytes, send_bytes, recv_bytes;
};

class echo_client : public client_base<echo_socket>
{
public:
	echo_client(service_pump& service_pump_) : client_base<echo_socket>(service_pump_) {}

	echo_socket::statistic get_statistic()
	{
		echo_socket::statistic stat;
		do_something_to_all([&stat](const auto& item) {stat += item->get_statistic(); });

		return stat;
	}

	void begin(size_t msg_num, const char* msg, size_t msg_len) {do_something_to_all([=](const auto& item) {item->begin(msg_num, msg, msg_len);});}
};

int main(int argc, const char* argv[])
{
	printf("usage: %s [<service thread number=1> [<port=%d> [<ip=%s> [link num=16]]]]\n", argv[0], ASCS_SERVER_PORT, ASCS_SERVER_IP);
	if (argc >= 2 && (0 == strcmp(argv[1], "--help") || 0 == strcmp(argv[1], "-h")))
		return 0;
	else
		puts("type " QUIT_COMMAND " to end.");

	///////////////////////////////////////////////////////////
	size_t link_num = 16;
	if (argc > 4)
		link_num = std::min(ASCS_MAX_OBJECT_NUM, std::max(atoi(argv[4]), 1));

	printf("exec: echo_client with " ASCS_SF " links\n", link_num);
	///////////////////////////////////////////////////////////

	service_pump sp;
	echo_client client(sp);

//	argv[2] = "::1" //ipv6
//	argv[2] = "127.0.0.1" //ipv4
	std::string ip = argc > 3 ? argv[3] : ASCS_SERVER_IP;
	unsigned short port = argc > 2 ? atoi(argv[2]) : ASCS_SERVER_PORT;

	auto thread_num = 1;
	if (argc > 1)
		thread_num = std::min(16, std::max(thread_num, atoi(argv[1])));
#ifdef ASCS_CLEAR_OBJECT_INTERVAL
	if (1 == thread_num)
		++thread_num;
#endif

	for (size_t i = 0; i < link_num; ++i)
		client.add_client(port, ip);

	sp.start_service(thread_num);
	while(sp.is_running())
	{
		std::string str;
		std::getline(std::cin, str);
		if (QUIT_COMMAND == str)
			sp.stop_service();
		else if (LIST_STATUS == str)
		{
			printf("link #: " ASCS_SF ", valid links: " ASCS_SF ", invalid links: " ASCS_SF "\n", client.size(), client.valid_size(), client.invalid_object_size());
			puts("");
			puts(client.get_statistic().to_string().data());
		}
		else if (!str.empty())
		{
			size_t msg_num = 1024;
			size_t msg_len = 1024; //must greater than or equal to sizeof(size_t)
			auto msg_fill = '0';

			auto parameters = split_string(str);
			auto iter = std::begin(parameters);
			if (iter != std::end(parameters)) msg_num = std::max((size_t) atoll(iter++->data()), (size_t) 1);
			if (iter != std::end(parameters)) msg_len = std::min((size_t) ASCS_MSG_BUFFER_SIZE, std::max((size_t) atoi(iter++->data()), (size_t) 1));
			if (iter != std::end(parameters)) msg_fill = *iter++->data();

			printf("test parameters after adjustment: " ASCS_SF " " ASCS_SF " %c\n", msg_num, msg_len, msg_fill);
			puts("performance test begin, this application will have no response during the test!");

			completed_session_num = (unsigned short) link_num;
			auto init_msg = new char[msg_len];
			memset(init_msg, msg_fill, msg_len);
			client.begin(msg_num, init_msg, msg_len);
			begin_time.restart();

			while (0 != completed_session_num)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

			uint64_t total_msg_bytes = link_num; total_msg_bytes *= msg_len; total_msg_bytes *= msg_num;
			printf("\r100%%\ntime spent statistics: %f seconds.\n", begin_time.elapsed());
			printf("speed: %f(*2) MBps.\n", total_msg_bytes / begin_time.elapsed() / 1024 / 1024);

			delete[] init_msg;
		}
	}

    return 0;
}

//restore configuration
#undef ASCS_SERVER_PORT
#undef ASCS_REUSE_OBJECT
#undef ASCS_FORCE_TO_USE_MSG_RECV_BUFFER
#undef ASCS_WANT_MSG_SEND_NOTIFY
#undef ASCS_DEFAULT_PACKER
#undef ASCS_DEFAULT_UNPACKER
#undef ASCS_MSG_BUFFER_SIZE
//restore configuration

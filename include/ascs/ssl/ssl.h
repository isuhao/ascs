/*
 * ssl.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * make ascs support asio::ssl
 */

#ifndef _ASICS_SSL_H_
#define _ASICS_SSL_H_

#include <asio/ssl.hpp>

#include "../object_pool.h"
#include "../tcp/connector.h"
#include "../tcp/client.h"
#include "../tcp/server_socket.h"
#include "../tcp/server.h"

namespace ascs { namespace ssl {

template <typename Socket>
class socket_helper : public Socket
{
#if defined(ASCS_REUSE_OBJECT) && !defined(ASCS_REUSE_SSL_STREAM)
	#error please define ASCS_REUSE_SSL_STREAM macro explicitly if you need asio::ssl::stream to be reusable!
#endif

public:
	template<typename Arg>
	socket_helper(Arg& arg, asio::ssl::context& ctx) : Socket(arg, ctx), authorized_(false) {shutdown_atomic.store(0, std::memory_order_relaxed);}

	virtual bool is_ready() {return authorized_ && Socket::is_ready();}
	virtual void reset() {authorized_ = false; shutdown_atomic.store(0, std::memory_order_relaxed); Socket::reset();}
	bool authorized() const {return authorized_;}

protected:
	virtual void on_recv_error(const asio::error_code& ec) {shutdown_ssl(); handle_last_step_of_shutdown(ec);}
	virtual void on_handshake(const asio::error_code& ec)
	{
		if (!ec)
			unified_out::info_out("handshake success.");
		else
			unified_out::error_out("handshake failed: %s", ec.message().data());
	}

	void handle_handshake(const asio::error_code& ec)
		{on_handshake(ec); if (ec) Socket::force_shutdown(); else {authorized_ = true; Socket::do_start();}}

	bool shutdown_ssl(bool sync = true)
	{
		if (!sync)
			unified_out::error_out("ascs only support sync mode when shutting down asio::ssl::stream!");

#ifdef ASCS_REUSE_SSL_STREAM
		return authorized_ = false;
#else
		bool re = false;
		if (is_ready())
		{
			size_t no_shutdown = 0;
			if (!shutdown_atomic.compare_exchange_strong(no_shutdown, 1, std::memory_order_acq_rel, std::memory_order_acquire))
				return true;

			this->show_info("ssl link:", "been shut down.");
			this->status = Socket::link_status::GRACEFUL_SHUTTING_DOWN;
			authorized_ = false;

			asio::error_code ec;
			this->next_layer().shutdown(ec); //asca only support sync mode, ignore sync parameter, it will always be true
			handle_last_step_of_shutdown(ec);

			re = !ec || asio::error::eof == ec; //the endpoint who initiated a shutdown will get error eof.
		}

		return re;
#endif
	}

private:
	void handle_last_step_of_shutdown(const asio::error_code& ec)
	{
		auto shutdown_status = shutdown_atomic.fetch_add(1, std::memory_order_acq_rel);
		if (0 == shutdown_status || shutdown_status > 1)
		{
			Socket::on_recv_error(ec);
			shutdown_atomic.store(0, std::memory_order_release);
		}
	}

protected:
	volatile bool authorized_;
	//shutdown_atomic:
	// 0-passive network error (in function on_recv_error)
	// 1-is calling shutdown
	// 2-shutdown has returned or on_recv_error has been called
	// 3-the last setp of shutdown
	std::atomic_size_t shutdown_atomic;
};

template <typename Packer, typename Unpacker, typename Socket = asio::ssl::stream<asio::ip::tcp::socket>,
	template<typename, typename> class InQueue = ASCS_INPUT_QUEUE, template<typename> class InContainer = ASCS_INPUT_CONTAINER,
	template<typename, typename> class OutQueue = ASCS_OUTPUT_QUEUE, template<typename> class OutContainer = ASCS_OUTPUT_CONTAINER>
class connector_base : public socket_helper<tcp::connector_base<Packer, Unpacker, Socket, InQueue, InContainer, OutQueue, OutContainer>>
{
protected:
	typedef socket_helper<tcp::connector_base<Packer, Unpacker, Socket, InQueue, InContainer, OutQueue, OutContainer>> super;

public:
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	connector_base(asio::io_service& io_service_, asio::ssl::context& ctx) : super(io_service_, ctx) {clear_reconnect_indicator();}

	virtual void reset() {super::reset(); clear_reconnect_indicator();}

	void disconnect(bool reconnect = false) {force_shutdown(reconnect);}
	void force_shutdown(bool reconnect = false) {graceful_shutdown(reconnect);}
	void graceful_shutdown(bool reconnect = false, bool sync = true)
	{
		if (!this->shutdown_ssl(sync))
			super::force_shutdown(reconnect);
		else if (reconnect)
			unified_out::error_out("you canot reuse asio::ssl::stream since macro ASCS_REUSE_SSL_STREAM not defined!");
	}

protected:
	virtual bool do_start() //add handshake
	{
		if (!this->is_connected())
			super::do_start();
		else if (!this->authorized())
			this->next_layer().async_handshake(asio::ssl::stream_base::client, this->make_handler_error([this](const auto& ec) {this->handle_handshake(ec);}));

		return true;
	}

#ifdef ASCS_REUSE_SSL_STREAM
	void clear_reconnect_indicator() {}
#else
	void clear_reconnect_indicator() {this->need_reconnect = false;}
	virtual int prepare_reconnect(const asio::error_code& ec) {return -1;}
#endif
	virtual void on_unpack_error() {unified_out::info_out("can not unpack msg."); force_shutdown();}
};

template<typename Object>
class object_pool : public ascs::object_pool<Object>
{
protected:
	typedef ascs::object_pool<Object> super;

public:
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	object_pool(service_pump& service_pump_, const asio::ssl::context::method& m) : super(service_pump_), ctx(m) {}
	asio::ssl::context& context() {return ctx;}

	using super::create_object;
	typename object_pool::object_type create_object() {return create_object(this->sp, ctx);}
	template<typename Arg>
	typename object_pool::object_type create_object(Arg& arg) {return create_object(arg, ctx);}

protected:
	asio::ssl::context ctx;
};

template<typename Packer, typename Unpacker, typename Server = i_server, typename Socket = asio::ssl::stream<asio::ip::tcp::socket>,
	template<typename, typename> class InQueue = ASCS_INPUT_QUEUE, template<typename> class InContainer = ASCS_INPUT_CONTAINER,
	template<typename, typename> class OutQueue = ASCS_OUTPUT_QUEUE, template<typename> class OutContainer = ASCS_OUTPUT_CONTAINER>
class server_socket_base : public socket_helper<tcp::server_socket_base<Packer, Unpacker, Server, Socket, InQueue, InContainer, OutQueue, OutContainer>>
{
protected:
	typedef socket_helper<tcp::server_socket_base<Packer, Unpacker, Server, Socket, InQueue, InContainer, OutQueue, OutContainer>> super;

public:
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	server_socket_base(Server& server_, asio::ssl::context& ctx) : super(server_, ctx) {}

	void disconnect() {force_shutdown();}
	void force_shutdown() {graceful_shutdown();}
	void graceful_shutdown(bool sync = true) {if (!this->shutdown_ssl(sync)) super::force_shutdown();}

protected:
	virtual bool do_start() //add handshake
	{
		if (!this->authorized())
			this->next_layer().async_handshake(asio::ssl::stream_base::server, this->make_handler_error([this](const auto& ec) {this->handle_handshake(ec);}));

		return true;
	}

	virtual void on_unpack_error() {unified_out::info_out("can not unpack msg."); force_shutdown();}
};

template<typename Socket, typename Pool = object_pool<Socket>, typename Server = i_server>
using server_base = tcp::server_base<Socket, Pool, Server>;

template<typename Socket, typename Pool = object_pool<Socket>>
using client_base = tcp::client_base<Socket, Pool>;

}} //namespace

#endif /* _ASICS_SSL_H_ */

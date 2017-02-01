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

#ifdef ASCS_REUSE_OBJECT
	#error asio::ssl::stream not support reusing!
#endif

namespace ascs { namespace ssl {

template <typename Packer, typename Unpacker, typename Socket = asio::ssl::stream<asio::ip::tcp::socket>,
	template<typename, typename> class InQueue = ASCS_INPUT_QUEUE, template<typename> class InContainer = ASCS_INPUT_CONTAINER,
	template<typename, typename> class OutQueue = ASCS_OUTPUT_QUEUE, template<typename> class OutContainer = ASCS_OUTPUT_CONTAINER>
class connector_base : public tcp::connector_base<Packer, Unpacker, Socket, InQueue, InContainer, OutQueue, OutContainer>
{
protected:
	typedef tcp::connector_base<Packer, Unpacker, Socket, InQueue, InContainer, OutQueue, OutContainer> super;

public:
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	connector_base(asio::io_service& io_service_, asio::ssl::context& ctx) : super(io_service_, ctx), authorized_(false) {this->need_reconnect = false;}

	virtual bool is_ready() {return authorized_ && super::is_ready();}
	virtual void reset() {authorized_ = false; super::reset(); this->need_reconnect = false;}
	bool authorized() const {return authorized_;}

	void disconnect(bool reconnect = false) {force_shutdown(reconnect);}
	void force_shutdown(bool reconnect = false)
	{
		if (reconnect)
			unified_out::error_out("asio::ssl::stream not support reconnecting!");

		if (!shutdown_ssl())
			super::force_shutdown(false);
	}

	//ssl only support sync mode, sync parameter will be ignored
	void graceful_shutdown(bool reconnect = false, bool sync = true)
	{
		if (reconnect)
			unified_out::error_out("asio::ssl::stream not support reconnecting!");

		if (!shutdown_ssl())
			super::force_shutdown(false);
	}

protected:
	virtual bool do_start() //add handshake
	{
		if (!this->is_connected())
			super::do_start();
		else if (!authorized_)
			this->next_layer().async_handshake(asio::ssl::stream_base::client, this->make_handler_error([this](const auto& ec) {this->handshake_handler(ec);}));
		else
			super::do_start();

		return true;
	}

	virtual void on_unpack_error() {unified_out::info_out("can not unpack msg."); force_shutdown();}
	virtual void on_recv_error(const asio::error_code& ec)
	{
		authorized_ = false;

		std::unique_lock<std::shared_mutex> lock(shutdown_mutex);
		super::on_recv_error(ec);
	}

	virtual void on_handshake(const asio::error_code& ec)
	{
		if (!ec)
			unified_out::info_out("handshake success.");
		else
			unified_out::error_out("handshake failed: %s", ec.message().data());
	}

	bool shutdown_ssl()
	{
		bool re = false;
		if (is_ready())
		{
			this->show_info("ssl client link:", "been shut down.");
			this->status = super::link_status::GRACEFUL_SHUTTING_DOWN;
			authorized_ = false;

			asio::error_code ec;
			std::unique_lock<std::shared_mutex> lock(shutdown_mutex);
			this->next_layer().shutdown(ec);
			lock.unlock();

			re = !ec;
		}

		return re;
	}

private:
	void handshake_handler(const asio::error_code& ec)
	{
		on_handshake(ec);
		if (!ec)
		{
			authorized_ = true;
			do_start();
		}
		else
			force_shutdown(false);
	}

protected:
	bool authorized_;
	std::shared_mutex shutdown_mutex;
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
class server_socket_base : public tcp::server_socket_base<Packer, Unpacker, Server, Socket, InQueue, InContainer, OutQueue, OutContainer>
{
protected:
	typedef tcp::server_socket_base<Packer, Unpacker, Server, Socket, InQueue, InContainer, OutQueue, OutContainer> super;

public:
	using super::TIMER_BEGIN;
	using super::TIMER_END;

	template<typename Arg>
	server_socket_base(Server& server_, Arg& arg) : super(server_, arg), authorized_(false) {}

	virtual bool is_ready() {return authorized_ && super::is_ready();}
	virtual void reset() {authorized_ = false; super::reset();}
	bool authorized() const {return authorized_;}

	void disconnect() {force_shutdown();}
	void force_shutdown() {if (!shutdown_ssl()) super::force_shutdown();}
	//ssl only support sync mode, sync parameter will be ignored
	void graceful_shutdown(bool sync = false) {if (!shutdown_ssl()) super::force_shutdown();}

protected:
	virtual bool do_start() //add handshake
	{
		if (!authorized_)
			this->next_layer().async_handshake(asio::ssl::stream_base::server, this->make_handler_error([this](const auto& ec) {this->handshake_handler(ec);}));
		else
			super::do_start();

		return true;
	}

	virtual void on_unpack_error() {unified_out::info_out("can not unpack msg."); force_shutdown();}
	virtual void on_recv_error(const asio::error_code& ec)
	{
		authorized_ = false;

		std::unique_lock<std::shared_mutex> lock(shutdown_mutex);
		super::on_recv_error(ec);
	}

	virtual void on_handshake(const asio::error_code& ec)
	{
		if (!ec)
			unified_out::info_out("handshake success.");
		else
			unified_out::error_out("handshake failed: %s", ec.message().data());
	}

	bool shutdown_ssl()
	{
		bool re = false;
		if (is_ready())
		{
			this->show_info("ssl server link:", "been shut down.");
			this->status = super::link_status::GRACEFUL_SHUTTING_DOWN;
			authorized_ = false;

			asio::error_code ec;
			std::unique_lock<std::shared_mutex> lock(shutdown_mutex);
			this->next_layer().shutdown(ec);
			lock.unlock();

			re = !ec;
		}

		return re;
	}

private:
	void handshake_handler(const asio::error_code& ec)
	{
		on_handshake(ec);
		if (!ec)
		{
			authorized_ = true;
			do_start();
		}
		else
		{
			force_shutdown();
			this->server.del_client(this->shared_from_this());
		}
	}

protected:
	bool authorized_;
	std::shared_mutex shutdown_mutex;
};

template<typename Socket, typename Pool = object_pool<Socket>, typename Server = i_server>
using server_base = tcp::server_base<Socket, Pool, Server>;

}} //namespace

#endif /* _ASICS_SSL_H_ */

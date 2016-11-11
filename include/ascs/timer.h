/*
 * timer.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * timer base class
 */

#ifndef _ASCS_TIMER_H_
#define _ASCS_TIMER_H_

#ifdef ASCS_USE_STEADY_TIMER
#include <asio/steady_timer.hpp>
#else
#include <asio/system_timer.hpp>
#endif

#include <set>
#include <chrono>

#include "object.h"

//If you inherit a class from class X, your own timer ids must begin from X::TIMER_END
namespace ascs
{

//timers are identified by id.
//for the same timer in the same timer, any manipulations are not thread safe, please pay special attention.
//to resolve this defect, we must add a mutex member variable to timer_info, it's not worth
//
//suppose you have more than one service thread(see service_pump for service thread number control), then:
//for same timer: same timer, on_timer is called sequentially
//for same timer: distinct timer, on_timer is called concurrently
//for distinct timer: on_timer is always called concurrently
class timer : public object
{
public:
	typedef std::chrono::milliseconds milliseconds;
#ifdef ASCS_USE_STEADY_TIMER
	typedef asio::steady_timer timer_type;
#else
	typedef asio::system_timer timer_type;
#endif

	typedef unsigned char tid;
	static const tid TIMER_END = 0; //user timer's id must begin from parent class' TIMER_END

	struct timer_info
	{
		enum timer_status {TIMER_FAKE, TIMER_OK, TIMER_CANCELED};

		tid id;
		mutable timer_status status;
		mutable size_t milliseconds;
		mutable std::function<bool(tid)> call_back;
		mutable std::shared_ptr<timer_type> timer;

		bool operator <(const timer_info& other) const {return id < other.id;}
	};

	typedef const timer_info timer_cinfo;
	typedef std::set<timer_info> container_type;

	timer(asio::io_service& _io_service_) : object(_io_service_)
	{
		timer_info ti = {0, timer_info::TIMER_FAKE, 0};
		for (auto i = 0; i < 256; ++i, ++ti.id)
			timer_can.insert(ti);
	}

	void update_timer_info(tid id, size_t milliseconds, std::function<bool(tid)>&& call_back, bool start = false)
	{
		timer_info ti = {id};

		auto iter = timer_can.find(ti); //must succeed
		if (timer_info::TIMER_FAKE == iter->status)
			iter->timer = std::make_shared<timer_type>(io_service_);
		iter->status = timer_info::TIMER_OK;
		iter->milliseconds = milliseconds;
		iter->call_back.swap(call_back);

		if (start)
			start_timer(*iter);
	}
	void update_timer_info(tid id, size_t milliseconds, const std::function<bool(tid)>& call_back, bool start = false)
		{update_timer_info(id, milliseconds, std::function<bool(tid)>(call_back), start);}

	void set_timer(tid id, size_t milliseconds, std::function<bool(tid)>&& call_back) {update_timer_info(id, milliseconds, std::move(call_back), true);}
	void set_timer(tid id, size_t milliseconds, const std::function<bool(tid)>& call_back) {update_timer_info(id, milliseconds, call_back, true);}

	timer_info find_timer(tid id)
	{
		timer_info ti = {id};
		return *timer_can.find(ti); //must succeed
	}

	bool start_timer(tid id)
	{
		timer_info ti = {id};

		auto iter = timer_can.find(ti); //must succeed
		if (timer_info::TIMER_FAKE == iter->status)
			return false;

		iter->status = timer_info::TIMER_OK;
		start_timer(*iter); //if timer already started, this will cancel it first

		return true;
	}

	void stop_timer(tid id)
	{
		timer_info ti = {id};
		stop_timer(*timer_can.find(ti)); //must succeed
	}

	DO_SOMETHING_TO_ALL(timer_can)
	DO_SOMETHING_TO_ONE(timer_can)

	void stop_all_timer() {do_something_to_all([this](const auto& item) {this->stop_timer(item);});}

protected:
	void reset() {object::reset();}

	void start_timer(timer_cinfo& ti)
	{
		assert(timer_info::TIMER_OK == ti.status);

		ti.timer->expires_from_now(milliseconds(ti.milliseconds));
		//return true from call_back to continue the timer, or the timer will stop
		ti.timer->async_wait(make_handler_error([this, &ti](const auto& ec) {if (!ec && ti.call_back(ti.id) && timer_info::TIMER_OK == ti.status) this->start_timer(ti);}));
	}

	void stop_timer(timer_cinfo& ti)
	{
		if (timer_info::TIMER_OK == ti.status) //enable stopping timers that has been stopped
		{
			asio::error_code ec;
			ti.timer->cancel(ec);
			ti.status = timer_info::TIMER_CANCELED;
		}
	}

	container_type timer_can;

private:
	using object::io_service_;
};

} //namespace

#endif /* _ASCS_TIMER_H_ */


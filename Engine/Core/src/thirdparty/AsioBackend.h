#ifndef MQENGINE_ASIO_BACKEND_H
#define MQENGINE_ASIO_BACKEND_H

#include "./thirdparty.h"

#ifndef BOOST_ASIO_NO_DEPRECATED
#define BOOST_ASIO_NO_DEPRECATED
#endif

#ifndef BOOST_ASIO_DISABLE_CO_AWAIT
#define BOOST_ASIO_DISABLE_CO_AWAIT
#endif

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#endif // MQENGINE_ASIO_BACKEND_H

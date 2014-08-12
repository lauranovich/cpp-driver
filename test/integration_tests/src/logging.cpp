#define BOOST_TEST_DYN_LINK
#ifdef STAND_ALONE
#   define BOOST_TEST_MODULE cassandra
#endif

#include <algorithm>

#include <boost/test/unit_test.hpp>
#include <boost/test/debug.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/cstdint.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/atomic.hpp>

#include "cassandra.h"
#include "test_utils.hpp"


namespace {

struct LogCount {
  LogCount() : log_count(0) {}
  boost::atomic<int> log_count;
};

} // namespace

void check_log_callback(cass_uint64_t time,
                        CassLogLevel severity,
                        CassString message,
                        void* data) {
  LogCount* log_count = reinterpret_cast<LogCount*>(data);
  log_count->log_count++;
}

struct LoggingTests : public test_utils::MultipleNodesTest {
  LoggingTests() : MultipleNodesTest(1, 0) {}
};

BOOST_FIXTURE_TEST_SUITE(logging, LoggingTests)

BOOST_AUTO_TEST_CASE(test_logging_callback)
{
  boost::shared_ptr<LogCount> log_count(new LogCount());

  {
    cass_cluster_set_log_level(cluster, CASS_LOG_DEBUG);
    cass_cluster_set_log_callback(cluster, check_log_callback, log_count.get());

    test_utils::CassFuturePtr session_future(cass_cluster_connect(cluster));
    test_utils::wait_and_check_error(session_future.get());
    test_utils::CassSessionPtr session(cass_future_get_session(session_future.get()));
  }

  BOOST_CHECK(log_count->log_count > 0);
}

BOOST_AUTO_TEST_SUITE_END()
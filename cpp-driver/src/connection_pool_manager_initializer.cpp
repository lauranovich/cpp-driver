/*
  Copyright (c) DataStax, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "connection_pool_manager_initializer.hpp"

#include "memory.hpp"
#include "request_queue.hpp"
#include "scoped_lock.hpp"

namespace cass {

ConnectionPoolManagerInitializer::ConnectionPoolManagerInitializer(EventLoop* event_loop,
                                                                   int protocol_version,
                                                                   void* data, Callback callback)
  : data_(data)
  , callback_(callback)
  , remaining_(0)
  , event_loop_(event_loop)
  , protocol_version_(protocol_version)
  , listener_(NULL)
  , metrics_(NULL) {
  uv_mutex_init(&lock_);
}

ConnectionPoolManagerInitializer::~ConnectionPoolManagerInitializer() {
  uv_mutex_destroy(&lock_);
}

void ConnectionPoolManagerInitializer::initialize(const AddressVec& addresses) {
  inc_ref();
  remaining_.store(addresses.size());
  manager_.reset(Memory::allocate<ConnectionPoolManager>(event_loop_,
                                                         protocol_version_,
                                                         keyspace_,
                                                         listener_,
                                                         metrics_,
                                                         settings_));
  for (AddressVec::const_iterator it = addresses.begin(),
       end = addresses.end(); it != end; ++it) {
    ConnectionPoolConnector::Ptr pool_connector(Memory::allocate<ConnectionPoolConnector>(manager_.get(),
                                                                                          *it,
                                                                                          this,
                                                                                          on_connect));
    pool_connector->connect();
  }
}

ConnectionPoolManagerInitializer* ConnectionPoolManagerInitializer::with_keyspace(const String& keyspace) {
  keyspace_ = keyspace;
  return this;
}

ConnectionPoolManagerInitializer* ConnectionPoolManagerInitializer::with_listener(ConnectionPoolManagerListener* listener) {
  listener_ = listener;
  return this;
}

ConnectionPoolManagerInitializer* ConnectionPoolManagerInitializer::with_metrics(Metrics* metrics) {
  metrics_ = metrics;
  return this;
}

ConnectionPoolManagerInitializer* ConnectionPoolManagerInitializer::with_settings(const ConnectionPoolManagerSettings& settings) {
  settings_ = settings;
  return this;
}

ConnectionPoolConnector::Vec ConnectionPoolManagerInitializer::failures() const {
  ScopedMutex l(&lock_);
  return failures_;
}

void ConnectionPoolManagerInitializer::on_connect(ConnectionPoolConnector* pool_connector) {
  ConnectionPoolManagerInitializer* initializer = static_cast<ConnectionPoolManagerInitializer*>(pool_connector->data());
  initializer->handle_connect(pool_connector);
}

void ConnectionPoolManagerInitializer::handle_connect(ConnectionPoolConnector* pool_connector) {
  { // Lock
    ScopedMutex l(&lock_);

    if (pool_connector->is_ok()) {
      manager_->add_pool(pool_connector->release_pool(), ConnectionPoolManager::Protected());
    } else {
      failures_.push_back(ConnectionPoolConnector::Ptr(pool_connector));
    }
  }

  if (remaining_.fetch_sub(1) - 1 == 0) {
    callback_(this);
    // If the manager hasn't been released then close it.
    if (manager_) manager_->close();
    dec_ref();
  }
}

} // namespace cass

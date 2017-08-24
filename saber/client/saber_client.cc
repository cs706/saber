// Copyright (c) 2017 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "saber/client/saber_client.h"

#include <set>
#include <utility>

#include "saber/client/client_watch_manager.h"
#include "saber/client/server_manager_impl.h"
#include "saber/net/messager.h"
#include "saber/util/logging.h"
#include "saber/util/timeops.h"

namespace saber {

SaberClient::SaberClient(voyager::EventLoop* loop, const ClientOptions& options,
                         Watcher* watcher)
    : has_started_(false),
      root_(options.root),
      server_manager_(options.server_manager),
      loop_(loop),
      session_id_(0),
      watch_manager_(new ClientWatchManager(options.auto_watch_reset)) {
  watch_manager_->SetDefaultWatcher(watcher);
}

SaberClient::~SaberClient() {
  if (has_started_) {
    Stop();
  }
}

void SaberClient::Start() {
  bool expected = false;
  if (has_started_.compare_exchange_strong(expected, true)) {
    Connect(server_manager_->GetNext());
  } else {
    LOG_WARN("SaberClient has started, don't call it again!");
  }
}

void SaberClient::Stop() {
  bool expected = true;
  if (has_started_.compare_exchange_strong(expected, false)) {
    Close();
  } else {
    LOG_WARN("SaberClient has stoped, don't call it again!");
  }
}

void SaberClient::Create(const CreateRequest& request, void* context,
                         const CreateCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_CREATE);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<CreateCallback>* r =
      new Request<CreateCallback>(request.path(), context, nullptr, cb);

  loop_->RunInLoop([this, message, r]() {
    create_queue_.push(std::unique_ptr<Request<CreateCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::Delete(const DeleteRequest& request, void* context,
                         const DeleteCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_DELETE);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<DeleteCallback>* r =
      new Request<DeleteCallback>(request.path(), context, nullptr, cb);

  loop_->RunInLoop([this, message, r]() {
    delete_queue_.push(std::unique_ptr<Request<DeleteCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::Exists(const ExistsRequest& request, Watcher* watcher,
                         void* context, const ExistsCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_EXISTS);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<ExistsCallback>* r =
      new Request<ExistsCallback>(request.path(), context, watcher, cb);

  loop_->RunInLoop([this, message, r]() {
    exists_queue_.push(std::unique_ptr<Request<ExistsCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::GetData(const GetDataRequest& request, Watcher* watcher,
                          void* context, const GetDataCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_GETDATA);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<GetDataCallback>* r =
      new Request<GetDataCallback>(request.path(), context, watcher, cb);

  loop_->RunInLoop([this, message, r]() {
    get_data_queue_.push(std::unique_ptr<Request<GetDataCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::SetData(const SetDataRequest& request, void* context,
                          const SetDataCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_SETDATA);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<SetDataCallback>* r =
      new Request<SetDataCallback>(request.path(), context, nullptr, cb);

  loop_->RunInLoop([this, message, r]() {
    set_data_queue_.push(std::unique_ptr<Request<SetDataCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::GetACL(const GetACLRequest& request, void* context,
                         const GetACLCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_GETACL);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<GetACLCallback>* r =
      new Request<GetACLCallback>(request.path(), context, nullptr, cb);

  loop_->RunInLoop([this, message, r]() {
    get_acl_queue_.push(std::unique_ptr<Request<GetACLCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::SetACL(const SetACLRequest& request, void* context,
                         const SetACLCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_SETACL);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<SetACLCallback>* r =
      new Request<SetACLCallback>(request.path(), context, nullptr, cb);

  loop_->RunInLoop([this, message, r]() {
    set_acl_queue_.push(std::unique_ptr<Request<SetACLCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::GetChildren(const GetChildrenRequest& request,
                              Watcher* watcher, void* context,
                              const GetChildrenCallback& cb) {
  SaberMessage* message = new SaberMessage();
  message->set_type(MT_GETCHILDREN);
  message->set_data(request.SerializeAsString());
  message->set_extra_data(root_);

  Request<GetChildrenCallback>* r =
      new Request<GetChildrenCallback>(request.path(), context, watcher, cb);

  loop_->RunInLoop([this, message, r]() {
    children_queue_.push(std::unique_ptr<Request<GetChildrenCallback> >(r));
    TrySendInLoop(message);
  });
}

void SaberClient::Connect(const voyager::SockAddr& addr) {
  client_.reset(new voyager::TcpClient(loop_, addr, "SaberClient"));
  client_->SetConnectionCallback(
      [this](const voyager::TcpConnectionPtr& p) { OnConnection(p); });
  client_->SetConnectFailureCallback([this]() { OnFailue(); });
  client_->SetCloseCallback(
      [this](const voyager::TcpConnectionPtr& p) { OnClose(p); });
  client_->SetMessageCallback(
      [this](const voyager::TcpConnectionPtr& p, voyager::Buffer* buf) {
        OnMessage(p, buf);
      });
  client_->Connect(false);
}

void SaberClient::Close() {
  loop_->RunInLoop([this]() {
    assert(client_);
    client_->Close();
  });
}

void SaberClient::TrySendInLoop(SaberMessage* message) {
  Messager::SendMessage(conn_wp_.lock(), *message);
  outgoing_queue_.push_back(std::unique_ptr<SaberMessage>(message));
}

void SaberClient::OnConnection(const voyager::TcpConnectionPtr& p) {
  LOG_DEBUG("SaberClient::OnConnection - connect successfully!");
  conn_wp_ = p;
  server_manager_->OnConnection();
  ConnectRequest request;
  request.set_session_id(session_id_);
  SaberMessage message;
  message.set_type(MT_CONNECT);
  message.set_data(request.SerializeAsString());
  message.set_extra_data(root_);
  Messager::SendMessage(p, message);
  for (auto& i : outgoing_queue_) {
    Messager::SendMessage(p, *i);
  }
}

void SaberClient::OnFailue() {
  LOG_DEBUG("SaberClient::OnFailue - connect failed!");
  if (has_started_) {
    Connect(server_manager_->GetNext());
  }
  master_.clear_host();
}

void SaberClient::OnClose(const voyager::TcpConnectionPtr& p) {
  LOG_DEBUG("SaberClient::OnClose - connect close!");
  if (has_started_) {
    if (master_.host().empty() == false) {
      voyager::SockAddr addr(master_.host(),
                             static_cast<uint16_t>(master_.port()));
      Connect(addr);
    } else {
      SleepForMicroseconds(1000);
      Connect(server_manager_->GetNext());
    }
  }
}

void SaberClient::OnMessage(const voyager::TcpConnectionPtr& p,
                            voyager::Buffer* buf) {
  Messager::OnMessage(
      p, buf,
      std::bind(&SaberClient::HandleMessage, this, std::placeholders::_1));
}

bool SaberClient::HandleMessage(std::unique_ptr<SaberMessage> message) {
  bool res = true;
  MessageType type = message->type();
  switch (type) {
    case MT_NOTIFICATION: {
      WatchedEvent* event = new WatchedEvent();
      event->ParseFromString(message->data());
      TriggerWatchers(event);
      break;
    }
    case MT_CONNECT: {
      ConnectResponse response;
      response.ParseFromString(message->data());
      session_id_ = response.session_id();
      timeout_ = response.timeout();
      WatchedEvent* event = new WatchedEvent();
      event->set_state(SS_CONNECTED);
      event->set_type(ET_NONE);
      TriggerWatchers(event);
      break;
    }
    case MT_CREATE: {
      CreateResponse* response = new CreateResponse();
      response->ParseFromString(message->data());
      auto& r = create_queue_.front();
      Request<CreateCallback>* request = r.release();
      create_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_DELETE: {
      DeleteResponse* response = new DeleteResponse();
      response->ParseFromString(message->data());
      auto& r = delete_queue_.front();
      Request<DeleteCallback>* request = r.release();
      delete_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_EXISTS: {
      ExistsResponse* response = new ExistsResponse();
      response->ParseFromString(message->data());
      auto& r = exists_queue_.front();
      if (r->watcher) {
        if (response->code() == RC_OK) {
          watch_manager_->AddDataWatch(r->path, r->watcher);
        } else {
          watch_manager_->AddExistWatch(r->path, r->watcher);
        }
      }
      Request<ExistsCallback>* request = r.release();
      exists_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_GETDATA: {
      GetDataResponse* response = new GetDataResponse();
      response->ParseFromString(message->data());
      auto& r = get_data_queue_.front();
      if (r->watcher && response->code() == RC_OK) {
        watch_manager_->AddDataWatch(r->path, r->watcher);
      }
      Request<GetDataCallback>* request = r.release();
      get_data_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_SETDATA: {
      SetDataResponse* response = new SetDataResponse();
      response->ParseFromString(message->data());
      auto& r = set_data_queue_.front();
      Request<SetDataCallback>* request = r.release();
      set_data_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_GETACL: {
      GetACLResponse* response = new GetACLResponse();
      response->ParseFromString(message->data());
      auto& r = get_acl_queue_.front();
      Request<GetACLCallback>* request = r.release();
      get_acl_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_SETACL: {
      SetACLResponse* response = new SetACLResponse();
      response->ParseFromString(message->data());
      auto& r = set_acl_queue_.front();
      Request<SetACLCallback>* request = r.release();
      set_acl_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_GETCHILDREN: {
      GetChildrenResponse* response = new GetChildrenResponse();
      response->ParseFromString(message->data());
      auto& r = children_queue_.front();
      if (r->watcher && response->code() == RC_OK) {
        watch_manager_->AddChildWatch(r->path, r->watcher);
      }
      Request<GetChildrenCallback>* request = r.release();
      children_queue_.pop();
      loop_->RunInLoop([request, response]() {
        request->callback(request->path, request->context, *response);
        delete request;
        delete response;
      });
      break;
    }
    case MT_MASTER: {
      res = false;
      master_.ParseFromString(message->data());
      LOG_DEBUG("The master is %s:%d.", master_.host().c_str(), master_.port());
      Close();
      break;
    }
    case MT_PING: {
      break;
    }
    default: {
      assert(false);
      LOG_ERROR("Invalid message type.");
      break;
    }
  }
  if (type != MT_NOTIFICATION && type != MT_MASTER && type != MT_PING &&
      type != MT_CONNECT) {
    assert(!outgoing_queue_.empty());
    outgoing_queue_.pop_front();
  }
  return res;
}

void SaberClient::TriggerWatchers(WatchedEvent* event) {
  WatcherSetPtr result = watch_manager_->Trigger(*event);
  // FIXME
  // Use std::shared_ptr to replace native pointer, which can avoid
  // memory leaks occur?
  std::set<Watcher*>* watchers = result.release();
  loop_->RunInLoop([watchers, event]() {
    if (watchers) {
      for (auto& it : *watchers) {
        it->Process(*event);
      }
      delete watchers;
    }
    delete event;
  });
}

}  // namespace saber

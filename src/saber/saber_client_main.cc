// Copyright (c) 2017 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <saber/client/saber.h>

void CreateCallback(const std::string& path, void* ctx,
                    const saber::CreateResponse& response) {

}

void GetDataCallback(const std::string& path, void* context,
                     const saber::GetDataResponse& response) {
  std::cout << "response:" << response.SerializeAsString() << std::endl;
}

void SetDataCallback(const std::string& path, void* context,
                     const saber::SetDataResponse& response) {
}

class DefaultWatcher : public saber::Watcher {
  public:
   DefaultWatcher() { }
   virtual void Process(const saber::WatchedEvent& event) {
   }
};

int main() {
  DefaultWatcher watcher;
  saber::Options options;
  options.group_size = 3;
  options.servers = "127.0.0.1:8888,127.0.0.1:8889";
  saber::Saber client(options);
  client.Start();
  saber::CreateRequest r1;
  client.Create(r1, nullptr, &CreateCallback);
  client.Connect();
  saber::GetDataRequest r2;
  client.GetData(r2, &watcher, nullptr, &GetDataCallback);
  saber::SetDataRequest r3;
  client.SetData(r3, nullptr, &SetDataCallback);
  while (true) {

  }
  return 0;
}

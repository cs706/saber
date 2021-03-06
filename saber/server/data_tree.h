// Copyright (c) 2017 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SABER_SERVER_DATA_TREE_H_
#define SABER_SERVER_DATA_TREE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "saber/proto/saber.pb.h"
#include "saber/proto/server.pb.h"
#include "saber/server/server_watch_manager.h"
#include "saber/service/acl.h"
#include "saber/util/mutex.h"

namespace saber {

class DataTree {
 public:
  DataTree();
  ~DataTree();

  uint64_t Recover(const std::string& s, size_t index);

  void Create(const CreateRequest& request, const Transaction* txn,
              CreateResponse* response, bool only_check = false);

  void Delete(const DeleteRequest& request, const Transaction* txn,
              DeleteResponse* response, bool only_check = false);

  void Exists(const ExistsRequest& request, Watcher* watcher,
              ExistsResponse* response);

  void GetData(const GetDataRequest& request, Watcher* watcher,
               GetDataResponse* response);

  void SetData(const SetDataRequest& request, const Transaction* txn,
               SetDataResponse* response, bool only_check = false);

  void GetACL(const GetACLRequest& request, GetACLResponse* response);

  void SetACL(const SetACLRequest& request, const Transaction* txn,
              SetACLResponse* response, bool only_check = false);

  void GetChildren(const GetChildrenRequest& request, Watcher* watcher,
                   GetChildrenResponse* response);

  void RemoveWatcher(Watcher* watcher);

  void KillSession(uint64_t session_id, const Transaction* txn);

  // No thread safe
  size_t NodeSize() const { return nodes_.size(); }

  // Serialize all nodes, and append the result to the *s;
  // No thread safe
  void SerializeToString(std::string* s) const;

  // Copy all the nodes
  // No thread safe
  // Caller should delete the return value when it's no longer needed.
  std::unordered_map<std::string, DataNode>* CopyNodes() const;

  // Copy all the childrens
  // No thread safe
  // Caller should delete the return value when it's no longer needed.
  std::unordered_map<std::string, std::unordered_set<std::string>>*
  CopyChildrens() const;

  // Serialize all nodes, and append the result to the *s.
  // Thread safe
  static void SerializeToString(
      const std::unordered_map<std::string, DataNode>& nodes,
      const std::unordered_map<std::string, std::unordered_set<std::string>>&
          childrens,
      std::string* s);

 private:
  // TODO
  bool CheckACL(const DataNode& node, Permissions perm,
                const std::vector<Id>* ids);

  static const bool kSkipACL = true;

  Mutex mutex_;
  std::unordered_map<std::string, DataNode> nodes_;
  std::unordered_map<std::string, std::unordered_set<std::string>> childrens_;

  std::unordered_map<uint64_t, std::unordered_set<std::string>> ephemerals_;

  ServerWatchManager data_watches_;
  ServerWatchManager child_watches_;

  // No copying allowed
  DataTree(const DataTree&);
  void operator=(const DataTree&);
};

}  // namespace saber

#endif  // SABER_SERVER_DATA_TREE_H_

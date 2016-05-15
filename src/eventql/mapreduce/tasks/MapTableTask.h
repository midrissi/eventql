/**
 * Copyright (c) 2016 zScale Technology GmbH <legal@zscale.io>
 * Authors:
 *   - Paul Asmuth <paul@zscale.io>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License ("the license") as
 * published by the Free Software Foundation, either version 3 of the License,
 * or any later version.
 *
 * In accordance with Section 7(e) of the license, the licensing of the Program
 * under the license does not imply a trademark license. Therefore any rights,
 * title and interest in our trademarks remain entirely with us.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the license for more details.
 *
 * You can be released from the requirements of the license by purchasing a
 * commercial license. Buying such a license is mandatory as soon as you develop
 * commercial activities involving this program without disclosing the source
 * code of your own applications
 */
#pragma once
#include "eventql/util/stdtypes.h"
#include "eventql/mapreduce/MapReduceTask.h"
#include "eventql/core/TSDBService.h"
#include "eventql/AnalyticsAuth.h"

using namespace stx;

namespace zbase {

struct MapTableTaskShard : public MapReduceTaskShard {
  TSDBTableRef table_ref;
};

class MapTableTask : public MapReduceTask {
public:

  MapTableTask(
      const AnalyticsSession& session,
      const TSDBTableRef& table,
      const String& map_function,
      const String& globals,
      const String& params,
      MapReduceShardList* shards,
      AnalyticsAuth* auth,
      zbase::PartitionMap* pmap,
      zbase::ReplicationScheme* repl);

  Option<MapReduceShardResult> execute(
      RefPtr<MapReduceTaskShard> shard,
      RefPtr<MapReduceScheduler> job) override;

  void setRequiredColumns(const Set<String>& columns);

protected:

  Option<MapReduceShardResult> executeRemote(
      RefPtr<MapTableTaskShard> shard,
      RefPtr<MapReduceScheduler> job,
      const ReplicaRef& host);

  AnalyticsSession session_;
  TSDBTableRef table_ref_;
  String map_function_;
  String globals_;
  String params_;
  AnalyticsAuth* auth_;
  zbase::PartitionMap* pmap_;
  zbase::ReplicationScheme* repl_;
  Set<String> required_columns_;
};

} // namespace zbase

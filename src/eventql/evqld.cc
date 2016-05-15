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
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "eventql/util/io/filerepository.h"
#include "eventql/util/io/fileutil.h"
#include "eventql/util/application.h"
#include "eventql/util/logging.h"
#include "eventql/util/random.h"
#include "eventql/util/thread/eventloop.h"
#include "eventql/util/thread/threadpool.h"
#include "eventql/util/thread/FixedSizeThreadPool.h"
#include "eventql/util/wallclock.h"
#include "eventql/util/VFS.h"
#include "eventql/util/rpc/ServerGroup.h"
#include "eventql/util/rpc/RPC.h"
#include "eventql/util/rpc/RPCClient.h"
#include "eventql/util/cli/flagparser.h"
#include "eventql/util/json/json.h"
#include "eventql/util/json/jsonrpc.h"
#include "eventql/util/http/httprouter.h"
#include "eventql/util/http/httpserver.h"
#include "eventql/util/http/VFSFileServlet.h"
#include "eventql/util/io/FileLock.h"
#include "eventql/util/stats/statsdagent.h"
#include "eventql/infra/sstable/SSTableServlet.h"
#include "eventql/util/mdb/MDB.h"
#include "eventql/util/mdb/MDBUtil.h"
#include "eventql/AnalyticsServlet.h"
#include "eventql/AnalyticsApp.h"
#include "eventql/TableDefinition.h"
#include "eventql/core/TSDBService.h"
#include "eventql/core/TSDBServlet.h"
#include "eventql/core/ReplicationWorker.h"
#include "eventql/core/LSMTableIndexCache.h"
#include "eventql/server/sql/sql_engine.h"
#include "eventql/DefaultServlet.h"
#include "eventql/sql/defaults.h"
#include "eventql/ConfigDirectory.h"
#include "eventql/StatusServlet.h"
#include <jsapi.h>

using namespace stx;
using namespace zbase;

stx::thread::EventLoop ev;

namespace js {
void DisableExtraThreads();
}

int main(int argc, const char** argv) {
  stx::Application::init();

  stx::cli::FlagParser flags;

  flags.defineFlag(
      "http_port",
      stx::cli::FlagParser::T_INTEGER,
      false,
      NULL,
      "8080",
      "Start the public http server on this port",
      "<port>");

  flags.defineFlag(
      "cachedir",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "cachedir path",
      "<path>");

  flags.defineFlag(
      "datadir",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "datadir path",
      "<path>");

#ifndef ZBASE_HAS_ASSET_BUNDLE
  flags.defineFlag(
      "asset_path",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      "src/",
      "assets path",
      "<path>");
#endif

  flags.defineFlag(
      "indexbuild_threads",
      cli::FlagParser::T_INTEGER,
      false,
      NULL,
      "2",
      "number of indexbuild threads",
      "<num>");

  flags.defineFlag(
      "master",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "url",
      "<addr>");

  flags.defineFlag(
      "join",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "url",
      "<name>");

  flags.defineFlag(
      "loglevel",
      stx::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.defineFlag(
      "daemonize",
      cli::FlagParser::T_SWITCH,
      false,
      NULL,
      NULL,
      "daemonize",
      "<switch>");

  flags.defineFlag(
      "pidfile",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "pidfile",
      "<path>");

  flags.defineFlag(
      "log_to_syslog",
      cli::FlagParser::T_SWITCH,
      false,
      NULL,
      NULL,
      "don't log to stderr",
      "<switch>");

  flags.defineFlag(
      "log_to_stderr",
      cli::FlagParser::T_SWITCH,
      false,
      NULL,
      "true",
      "don't log to stderr",
      "<switch>");

  flags.parseArgv(argc, argv);

  if (flags.isSet("log_to_stderr") && !flags.isSet("daemonize")) {
    stx::Application::logToStderr();
  }

  if (flags.isSet("log_to_syslog")) {
    stx::Application::logToSyslog("z1d");
  }

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  if (flags.isSet("daemonize")) {
    Application::daemonize();
  }

  ScopedPtr<FileLock> pidfile_lock;
  if (flags.isSet("pidfile")) {
    pidfile_lock = mkScoped(new FileLock(flags.getString("pidfile")));
    pidfile_lock->lock(false);

    auto pidfile = File::openFile(
        flags.getString("pidfile"),
        File::O_WRITE | File::O_CREATEOROPEN | File::O_TRUNCATE);

    pidfile.write(StringUtil::toString(getpid()));
  }

  /* conf */
  //auto conf_data = FileUtil::read(flags.getString("conf"));
  //auto conf = msg::parseText<zbase::TSDBNodeConfig>(conf_data);

  /* thread pools */
  stx::thread::CachedThreadPool tpool(
      thread::ThreadPoolOptions {
        .thread_name = Some(String("z1d-httpserver"))
      },
      8);

  /* http */
  stx::http::HTTPRouter http_router;
  stx::http::HTTPServer http_server(&http_router, &ev);
  http_server.listen(flags.getInt("http_port"));
  http::HTTPConnectionPool http(&ev, &z1stats()->http_client_stats);

  /* customer directory */
  if (!FileUtil::exists(flags.getString("datadir"))) {
    RAISE(kRuntimeError, "data dir not found: " + flags.getString("datadir"));
  }

  auto cdb_dir = FileUtil::joinPaths(flags.getString("datadir"), "cdb");
  if (!FileUtil::exists(cdb_dir)) {
    FileUtil::mkdir(cdb_dir);
  }

  ConfigDirectory customer_dir(
      cdb_dir,
      InetAddr::resolve(flags.getString("master")),
      ConfigTopic::CUSTOMERS | ConfigTopic::TABLES | ConfigTopic::USERDB |
      ConfigTopic::CLUSTERCONFIG);

  /* clusterconfig */
  auto cluster_config = customer_dir.clusterConfig();
  logInfo(
      "zbase",
      "Starting with cluster config: $0",
      cluster_config.DebugString());

  /* tsdb */
  Option<String> local_replica;
  if (flags.isSet("join")) {
    local_replica = Some(flags.getString("join"));
  }

  auto repl_scheme = RefPtr<zbase::ReplicationScheme>(
        new zbase::DHTReplicationScheme(cluster_config, local_replica));

  String node_name = "__anonymous";
  if (flags.isSet("join")) {
    node_name = flags.getString("join");
  }

  auto tsdb_dir = FileUtil::joinPaths(
      flags.getString("datadir"),
      "data/" + node_name);

  if (!FileUtil::exists(tsdb_dir)) {
    FileUtil::mkdir_p(tsdb_dir);
  }

  auto trash_dir = FileUtil::joinPaths(flags.getString("datadir"), "trash");
  if (!FileUtil::exists(trash_dir)) {
    FileUtil::mkdir(trash_dir);
  }

  FileLock server_lock(FileUtil::joinPaths(tsdb_dir, "__lock"));
  server_lock.lock();

  zbase::ServerConfig cfg;
  cfg.db_path = tsdb_dir;
  cfg.repl_scheme = repl_scheme;
  cfg.idx_cache = mkRef(new LSMTableIndexCache(tsdb_dir));

  zbase::PartitionMap partition_map(&cfg);
  zbase::TSDBService tsdb_node(
      &partition_map,
      repl_scheme.get(),
      &ev,
      &z1stats()->http_client_stats);

  zbase::ReplicationWorker tsdb_replication(
      repl_scheme.get(),
      &partition_map,
      &http);

  zbase::TSDBServlet tsdb_servlet(&tsdb_node, flags.getString("cachedir"));
  http_router.addRouteByPrefixMatch("/tsdb", &tsdb_servlet, &tpool);

  zbase::CompactionWorker cstable_index(
      &partition_map,
      flags.getInt("indexbuild_threads"));

  /* sql */
  RefPtr<csql::Runtime> sql;
  {
    auto symbols = mkRef(new csql::SymbolTable());
    csql::installDefaultSymbols(symbols.get());
    sql = mkRef(new csql::Runtime(
        stx::thread::ThreadPoolOptions {
          .thread_name = Some(String("z1d-sqlruntime"))
        },
        symbols,
        new csql::QueryBuilder(
            new csql::ValueExpressionBuilder(symbols.get())),
        new csql::QueryPlanBuilder(
            csql::QueryPlanBuilderOptions{},
            symbols.get())));

    sql->setCacheDir(flags.getString("cachedir"));
    sql->symbols()->registerFunction("z1_version", &z1VersionExpr);
  }

  /* spidermonkey javascript runtime */
  JS_Init();
  js::DisableExtraThreads();

  /* analytics core */
  AnalyticsAuth auth(&customer_dir);

  auto analytics_app = mkRef(
      new AnalyticsApp(
          &tsdb_node,
          &partition_map,
          repl_scheme.get(),
          &cstable_index,
          &customer_dir,
          &auth,
          sql.get(),
          nullptr,
          flags.getString("datadir"),
          flags.getString("cachedir")));

  zbase::AnalyticsServlet analytics_servlet(
      analytics_app,
      flags.getString("cachedir"),
      &auth,
      sql.get(),
      &tsdb_node,
      &customer_dir,
      &partition_map);

  zbase::StatusServlet status_servlet(
      &cfg,
      &partition_map,
      http_server.stats(),
      &z1stats()->http_client_stats);

  zbase::DefaultServlet default_servlet;

  http_router.addRouteByPrefixMatch("/api/", &analytics_servlet, &tpool);
  http_router.addRouteByPrefixMatch("/zstatus", &status_servlet);
  http_router.addRouteByPrefixMatch("/", &default_servlet);

  auto rusage_t = std::thread([] () {
    for (;; usleep(1000000)) {
      logDebug(
          "zbase",
          "Using $0MB of memory (peak $1)",
          Application::getCurrentMemoryUsage() / 1000000.0,
          Application::getPeakMemoryUsage() / 1000000.0);
    }
  });

  rusage_t.detach();

  Application::setCurrentThreadName("z1d");

  try {
    partition_map.open();
    customer_dir.startWatcher();
    ev.run();
  } catch (const StandardException& e) {
    logAlert("zbase", e, "FATAL ERROR");
  }

  stx::logInfo("zbase", "Exiting...");

  customer_dir.stopWatcher();

  JS_ShutDown();

  if (flags.isSet("pidfile")) {
    pidfile_lock.reset(nullptr);
    FileUtil::rm(flags.getString("pidfile"));
  }

  exit(0);
}

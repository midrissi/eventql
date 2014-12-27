/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <stdlib.h>
#include <unistd.h>
#include "fnord/base/random.h"
#include "fnord/comm/rpc.h"
#include "fnord/comm/rpcchannel.h"
#include "fnord/io/filerepository.h"
#include "fnord/json/json.h"
#include "fnord/net/http/httprouter.h"
#include "fnord/net/http/httpserver.h"
#include "fnord/thread/eventloop.h"
#include "fnord/thread/threadpool.h"
#include "fnord/service/logstream/logstreamservice.h"
#include "fnord/service/logstream/feedfactory.h"
#include "fnord/system/signalhandler.h"
#include "fnord/logging/logoutputstream.h"
#include "customernamespace.h"
#include "tracker/tracker.h"
#include "tracker/logjoinservice.h"

using fnord::comm::LocalRPCChannel;

int main() {
  fnord::system::SignalHandler::ignoreSIGHUP();
  fnord::system::SignalHandler::ignoreSIGPIPE();

  fnord::CatchAndAbortExceptionHandler ehandler;
  ehandler.installGlobalHandlers();

  fnord::log::LogOutputStream logger(fnord::io::OutputStream::getStderr());
  fnord::log::Logger::get()->setMinimumLogLevel(fnord::log::kDebug);
  fnord::log::Logger::get()->listen(&logger);

  fnord::thread::ThreadPool thread_pool;
  fnord::thread::EventLoop event_loop;
  fnord::comm::RPCServiceMap service_map;


  /* set up customers */
  auto dwn_ns = new cm::CustomerNamespace("dawanda");
  dwn_ns->addVHost("dwnapps.net");
  dwn_ns->loadTrackingJS("config/c_dwn/track.js");



  /* set up logstream service */
  fnord::logstream_service::LogStreamService logstream_service(
      fnord::io::FileRepository("/tmp/cm-logs"));

  service_map.addChannel(
      "cm.tracker.logstream_in",
      LocalRPCChannel::forService(&logstream_service, &thread_pool));

  service_map.addChannel(
      "cm.tracker.logstream_out",
      LocalRPCChannel::forService(&logstream_service, &thread_pool));



  /* set up tracker */
  fnord::logstream_service::LogStreamServiceFeedFactory feeds(
      service_map.getChannel("cm.tracker.logstream_in"));

  cm::Tracker tracker(&feeds);
  tracker.addCustomer(dwn_ns);



  /* set up http server */
  fnord::http::HTTPRouter http_router;
  //http_router.addRouteByPrefixMatch("/rpc", &tracker);
  http_router.addRouteByPrefixMatch("/t", &tracker);
  fnord::http::HTTPServer http_server(&http_router, &event_loop);
  http_server.listen(8080);

  event_loop.run();

  return 0;
}


/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include "stx/wallclock.h"
#include "stx/assets.h"
#include <stx/fnv.h>
#include "stx/protobuf/msg.h"
#include "zbase/metrics/MetricAPIServlet.h"
#include "zbase/metrics/MetricQuery.h"

using namespace stx;

namespace zbase {

MetricAPIServlet::MetricAPIServlet(
    MetricService* service,
    ConfigDirectory* cdir,
    const String& cachedir) :
    service_(service),
    cdir_(cdir),
    cachedir_(cachedir) {}

void MetricAPIServlet::handle(
    const AnalyticsSession& session,
    RefPtr<stx::http::HTTPRequestStream> req_stream,
    RefPtr<stx::http::HTTPResponseStream> res_stream) {
  const auto& req = req_stream->request();
  URI uri(req.uri());

  http::HTTPResponse res;
  res.populateFromRequest(req);

  if (uri.path() == "/api/v1/metrics/query") {
    executeQuery(session, uri, req_stream.get(), res_stream.get());
    return;
  }

  res.setStatus(http::kStatusNotFound);
  res.addHeader("Content-Type", "text/html; charset=utf-8");
  res.addBody(Assets::getAsset("zbase/webui/404.html"));
  res_stream->writeResponse(res);
}

void MetricAPIServlet::executeQuery(
    const AnalyticsSession& session,
    const URI& uri,
    http::HTTPRequestStream* req_stream,
    http::HTTPResponseStream* res_stream) {
  req_stream->readBody();

  auto sse_stream = mkRef(new http::HTTPSSEStream(req_stream, res_stream));
  sse_stream->start();

  auto send_progress = [this, sse_stream] (double progress) {
    if (sse_stream->isClosed()) {
      return;
    }

    Buffer buf;
    json::JSONOutputStream json(BufferOutputStream::fromBuffer(&buf));
    json.beginObject();
    json.addObjectEntry("status");
    json.addString("running");
    json.addComma();
    json.addObjectEntry("progress");
    json.addFloat(progress);
    json.endObject();
    sse_stream->sendEvent(buf, Some(String("status")));
  };

  try {
    auto query = mkRef(new MetricQuery());
    query->onProgress(send_progress);

    send_progress(0);
    service_->executeQuery(session, query);

    {
      Buffer buf;
      json::JSONOutputStream json(BufferOutputStream::fromBuffer(&buf));
      json.beginObject();
      json.endObject();
      sse_stream->sendEvent(buf, Some(String("result")));
    }
  } catch (const StandardException& e) {
    Buffer buf;
    json::JSONOutputStream json(BufferOutputStream::fromBuffer(&buf));
    json.beginObject();
    json.addObjectEntry("status");
    json.addString("error");
    json.addComma();
    json.addObjectEntry("error");
    json.addString(e.what());
    json.endObject();

    sse_stream->sendEvent(buf, Some(String("status")));
    sse_stream->sendEvent(URI::urlEncode(e.what()), Some(String("error")));
  }

  sse_stream->finish();
}

}
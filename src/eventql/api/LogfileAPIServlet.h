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
#include "eventql/util/http/httpservice.h"
#include "eventql/util/http/HTTPSSEStream.h"
#include "eventql/AnalyticsSession.pb.h"
#include "eventql/api/LogfileService.h"

using namespace stx;

namespace zbase {

class LogfileAPIServlet {
public:

  LogfileAPIServlet(
      LogfileService* service,
      ConfigDirectory* cdir,
      const String& cachedir);

  void handle(
      const AnalyticsSession& session,
      RefPtr<stx::http::HTTPRequestStream> req_stream,
      RefPtr<stx::http::HTTPResponseStream> res_stream);

protected:

  void listLogfiles(
      const AnalyticsSession& session,
      const URI& uri,
      const http::HTTPRequest* req,
      http::HTTPResponse* res);

  void fetchLogfileDefinition(
      const AnalyticsSession& session,
      const URI& uri,
      const http::HTTPRequest* req,
      http::HTTPResponse* res);

  void setLogfileRegex(
      const AnalyticsSession& session,
      const URI& uri,
      const http::HTTPRequest* req,
      http::HTTPResponse* res);

  void uploadLogfile(
      const AnalyticsSession& session,
      const URI& uri,
      http::HTTPRequestStream* req_stream,
      http::HTTPResponse* res);

  void renderLogfileDefinition(
      const LogfileDefinition* logfile_def,
      json::JSONOutputStream* json);

  LogfileService* service_;
  ConfigDirectory* cdir_;
  String cachedir_;
};

}
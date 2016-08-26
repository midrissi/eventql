/**
 * Copyright (c) 2016 DeepCortex GmbH <legal@eventql.io>
 * Authors:
 *   - Paul Asmuth <paul@eventql.io>
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
#include "eventql/transport/native/native_transport.h"
#include "eventql/transport/native/native_connection.h"
#include "eventql/util/logging.h"
#include "eventql/util/util/binarymessagereader.h"
#include "eventql/server/session.h"
#include "eventql/server/sql_service.h"
#include "eventql/sql/runtime/runtime.h"
#include "eventql/auth/client_auth.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

namespace eventql {
namespace native_transport {

ReturnCode sendError(
    NativeConnection* conn,
    const std::string& error) {
  util::BinaryMessageWriter e_frame;
  e_frame.appendLenencString(error);
  char zero = 0;
  e_frame.append(&zero, 1);
  return conn->sendFrame(EVQL_OP_ERROR, e_frame.data(), e_frame.size());
}

ReturnCode performOperation_QUERY(
    Database* database,
    NativeConnection* conn,
    const std::string& payload) {
  auto session = database->getSession();
  auto dbctx = session->getDatabaseContext();

  /* read query frame */
  std::string q_query;
  uint64_t q_flags;
  uint64_t q_maxrows;
  std::string q_database;
  try {
    util::BinaryMessageReader q_frame(payload.data(), payload.size());
    q_query = q_frame.readLenencString();
    q_flags = q_frame.readVarUInt();
    q_maxrows = q_frame.readVarUInt();
    q_database = q_frame.readLenencString();
  } catch (const std::exception& e) {
    return ReturnCode::error("ERUNTIME", "invalid QUERY frame");
  }

  /* switch database */
  if (q_flags & EVQL_QUERY_SWITCHDB) {
    auto rc = dbctx->client_auth->changeNamespace(session, q_database);
    if (!rc.isSuccess()) {
      return sendError(conn, rc.message());
    }
  }

  if (session->getEffectiveNamespace().empty()) {
    return sendError(conn, "No database selected");
  }

  /* execute queries */
  try {
    auto txn = dbctx->sql_service->startTransaction(session);
    auto qplan = dbctx->sql_runtime->buildQueryPlan(txn.get(), q_query);

    //qplan->setProgressCallback([&result_format, &qplan] () {
    //  result_format.sendProgress(qplan->getProgress());
    //});

    if (qplan->numStatements() > 1 && !(q_flags & EVQL_QUERY_MULTISTMT)) {
      return sendError(
          conn,
          "you must set EVQL_QUERY_MULTISTMT to enable multiple statements");
    }

    for (int i = 0; i < qplan->numStatements(); ++i) {
      /* execute query */
      auto result_columns = qplan->getStatementgetResultColumns(i);
      auto result_cursor = qplan->execute(i);
      auto result_ncols = result_cursor->getNumColumns();

      /* send response frames */
      Vector<csql::SValue> row(result_ncols);
      while (result_cursor->next(row.data(), row.size())) {

        /* wait for discard or continue */
      }

      /* send final response frame */

      /* wait for discard or continue (next query) */
    }
  } catch (const StandardException& e) {
    return sendError(conn, e.what());
  }

  return ReturnCode::success();
}

ReturnCode performOperation(
    Database* database,
    NativeConnection* conn,
    uint16_t opcode,
    const std::string& payload) {
  logDebug("eventql", "Performing operation; opcode=$0", opcode);

  switch (opcode) {
    case EVQL_OP_QUERY:
      return performOperation_QUERY(database, conn, payload);
    default:
      conn->close();
      return ReturnCode::error("ERUNTIME", "invalid opcode");
  }

  return ReturnCode::success();
}

ReturnCode performHandshake(Database* database, NativeConnection* conn) {
  /* read HELLO frame */
  {
    uint16_t opcode;
    std::string payload;
    auto rc = conn->recvFrame(&opcode, &payload);
    if (!rc.isSuccess()) {
      conn->close();
      return rc;
    }

    switch (opcode) {
      case EVQL_OP_HELLO:
        break;
      default:
        conn->close();
        return ReturnCode::error("ERUNTIME", "invalid opcode");
    }
  }

  /* send READY frame */
  {
    auto rc = conn->sendFrame(EVQL_OP_READY, nullptr, 0);
    if (!rc.isSuccess()) {
      conn->close();
      return rc;
    }
  }

  return ReturnCode::success();
}

void startConnection(Database* db, int fd, std::string prelude_bytes) {
  logDebug("eventql", "Opening new native connection; fd=$0", fd);

  int old_flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) != 0) {
    close(fd);
    return;
  }

  db->startThread([db, fd, prelude_bytes] (Session* session) {
    NativeConnection conn(fd, prelude_bytes);

    auto rc = performHandshake(db, &conn);
    if (!rc.isSuccess()) {
      logError("eventql", "Handshake error: $0", rc.getMessage());
      conn.close();
      return;
    }

    logDebug("eventql", "Native connection established; fd=$0", fd);
    uint16_t opcode;
    std::string payload;
    while (rc.isSuccess()) {
      rc = conn.recvFrame(&opcode, &payload);

      if (rc.isSuccess()) {
        rc = performOperation(db, &conn, opcode, payload);
      }
    }

    conn.close();
  });
}

} // namespace native_transport
} // namespace eventql

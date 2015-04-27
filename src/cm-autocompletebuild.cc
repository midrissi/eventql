/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "fnord-base/io/fileutil.h"
#include "fnord-base/application.h"
#include "fnord-base/logging.h"
#include "fnord-base/Language.h"
#include "fnord-base/random.h"
#include "fnord-base/cli/flagparser.h"
#include "fnord-base/util/SimpleRateLimit.h"
#include "fnord-base/InternMap.h"
#include "fnord-base/thread/threadpool.h"
#include "fnord-json/json.h"
#include "fnord-mdb/MDB.h"
#include "fnord-mdb/MDBUtil.h"
#include "fnord-sstable/sstablereader.h"
#include "fnord-sstable/sstablewriter.h"
#include "fnord-sstable/SSTableColumnSchema.h"
#include "fnord-sstable/SSTableColumnReader.h"
#include "fnord-sstable/SSTableColumnWriter.h"
#include "fnord-logtable/ArtifactIndex.h"
#include <fnord-fts/fts.h>
#include <fnord-fts/fts_common.h>
#include "fnord-logtable/TableReader.h"
#include "common.h"
#include "schemas.h"
#include "CustomerNamespace.h"
#include "CTRCounter.h"
#include "analytics/ReportBuilder.h"
#include "analytics/AnalyticsTableScanSource.h"
#include "analytics/CTRBySearchTermCrossCategoryMapper.h"
#include "analytics/CTRCounterMergeReducer.h"
#include "analytics/CTRCounterTableSink.h"
#include "analytics/CTRCounterTableSource.h"
#include "analytics/RelatedTermsMapper.h"
#include "analytics/TopCategoriesByTermMapper.h"
#include "analytics/TermInfoMergeReducer.h"

using namespace fnord;
using namespace cm;

int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  fnord::cli::FlagParser flags;

  //flags.defineFlag(
  //    "conf",
  //    cli::FlagParser::T_STRING,
  //    false,
  //    NULL,
  //    "./conf",
  //    "conf directory",
  //    "<path>");

  //flags.defineFlag(
  //    "index",
  //    cli::FlagParser::T_STRING,
  //    false,
  //    NULL,
  //    NULL,
  //    "index directory",
  //    "<path>");

  flags.defineFlag(
      "replica",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "replica id",
      "<id>");

  flags.defineFlag(
      "datadir",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "artifact directory",
      "<path>");

  flags.defineFlag(
      "tempdir",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "artifact directory",
      "<path>");

  flags.defineFlag(
      "loglevel",
      fnord::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.parseArgv(argc, argv);

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  auto tempdir = flags.getString("tempdir");
  auto datadir = flags.getString("datadir");

  thread::ThreadPool tpool;
  cm::ReportBuilder report_builder(&tpool);

  auto buildid = "golden";

  //Random rnd;
  //auto buildid = rnd.hex128();

  //auto table = logtable::TableReader::open(
  //    "joined_sessions-dawanda",
  //    flags.getString("replica"),
  //    flags.getString("datadir"),
  //    joinedSessionsSchema());

  //auto snap = table->getSnapshot();

  //Set<String> searchterm_x_e1_tables;
  //Set<String> related_terms_tables;

  //for (const auto& c : snap->head->chunks) {
  //  auto input_table = StringUtil::format(
  //      "$0/$1.$2.$3.cst",
  //      datadir,
  //      "joined_sessions-dawanda",
  //      c.replica_id,
  //      c.chunk_id);

  //  /* map related terms */
  //  auto related_terms_table = StringUtil::format(
  //      "$0/dawanda_related_terms.$1.$2.sst",
  //      tempdir,
  //      c.replica_id,
  //      c.chunk_id);

  //  related_terms_tables.emplace(related_terms_table);
  //  report_builder.addReport(
  //      new RelatedTermsMapper(
  //          new AnalyticsTableScanSource(input_table),
  //          new TermInfoTableSink(related_terms_table)));

  //  /* map serchterm x e1 */
  //  auto searchterm_x_e1_table = StringUtil::format(
  //      "$0/dawanda_ctr_by_searchterm_cross_e1.$1.$2.sst",
  //      tempdir,
  //      c.replica_id,
  //      c.chunk_id);

  //  searchterm_x_e1_tables.emplace(searchterm_x_e1_table);
  //  report_builder.addReport(
  //      new CTRBySearchTermCrossCategoryMapper(
  //          new AnalyticsTableScanSource(input_table),
  //          new CTRCounterTableSink(0, 0, searchterm_x_e1_table),
  //          "category1"));
  //}

  //report_builder.addReport(
  //    new TermInfoMergeReducer(
  //        new TermInfoTableSource(related_terms_tables),
  //        new TermInfoTableSink(
  //            StringUtil::format(
  //                "$0/dawanda_related_terms.$1.sst",
  //                tempdir,
  //                buildid))));

  //report_builder.addReport(
  //    new TopCategoriesByTermMapper(
  //        new CTRCounterTableSource(searchterm_x_e1_tables),
  //        new TermInfoTableSink(
  //            StringUtil::format(
  //                "$0/dawanda_top_cats_by_searchterm_e1.$1.sst",
  //                tempdir,
  //                buildid)),
  //        "e1-"));

  //report_builder.addReport(
  //    new TermInfoMergeReducer(
  //        new TermInfoTableSource(Set<String> {
  //          StringUtil::format(
  //                "$0/dawanda_related_terms.$1.sst",
  //                tempdir,
  //                buildid),
  //          StringUtil::format(
  //                "$0/dawanda_top_cats_by_searchterm_e1.$1.sst",
  //                tempdir,
  //                buildid)
  //        }),
  //        new TermInfoTableSink(
  //            StringUtil::format(
  //                "$0/dawanda_termstats.$1.sst",
  //                tempdir,
  //                buildid))));

  //report_builder.buildAll();

  fnord::logInfo(
      "cm.reportbuild",
      "Build completed: dawanda_termstats.$0.sst",
      buildid);

  logtable::ArtifactIndex artifacts(datadir, "termstats", false);

  auto outfile = StringUtil::format("termstats-dawanda.$0.sst", buildid);
  auto tempfile_path = FileUtil::joinPaths(tempdir, outfile);
  auto outfile_path = FileUtil::joinPaths(datadir, outfile);

  FileUtil::mv(tempfile_path, outfile_path);

  logtable::ArtifactRef afx;
  afx.name = StringUtil::format("termstats-dawanda.$0", buildid);
  afx.status = logtable::ArtifactStatus::PRESENT;
  afx.attributes.emplace_back(
      "built_at",
      StringUtil::toString(WallClock::unixMicros() / kMicrosPerSecond));
  afx.files.emplace_back(logtable::ArtifactFileRef {
    .filename = outfile,
    .size = FileUtil::size(outfile_path),
    .checksum = FileUtil::checksum(outfile_path)
  });

  artifacts.addArtifact(afx);

  return 0;
}


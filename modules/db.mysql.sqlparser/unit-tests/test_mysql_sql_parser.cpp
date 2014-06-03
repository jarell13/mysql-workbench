/* 
 * Copyright (c) 2011, 2014, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef _WIN32
#include <sstream>
#endif

#include <iostream>
#include <fstream>

#include "grt_test_utility.h"
#include "testgrt.h"
#include "grtsqlparser/sql_facade.h"
#include "wb_helpers.h"
#include "backend/db_mysql_sql_export.h"

BEGIN_TEST_DATA_CLASS(mysql_sql_parser)
public:
  WBTester wbt;
  SqlFacade::Ref sql_facade;
  db_mgmt_RdbmsRef rdbms;
  DictRef options;
  void test_import_sql(int test_no, const char *old_schema_name= NULL, const char *new_schema_name= NULL);

END_TEST_DATA_CLASS


TEST_MODULE(mysql_sql_parser, "SQL Parser (MySQL)");


TEST_FUNCTION(1)
{
  wbt.create_new_document();
  GRT *grt= wbt.grt;

  ensure_equals("loaded physycal model count", wbt.wb->get_document()->physicalModels().count(), 1U);

  options= DictRef(grt);
  options.set("gen_fk_names_when_empty", IntegerRef(0));

  rdbms= wbt.wb->get_document()->physicalModels().get(0)->rdbms();

  sql_facade= SqlFacade::instance_for_rdbms(rdbms);
  ensure("failed to get sqlparser module", (NULL != sql_facade));
}


void Test_object_base<mysql_sql_parser>::test_import_sql(int test_no, const char *old_schema_name, const char *new_schema_name)
{
  static const char* TEST_DATA_DIR = "data/modules_grt/wb_mysql_import/sql/";

  /* set filenames & messages based on test no. */
  std::ostringstream oss;
  oss << test_no;
  std::string test_message =                "SQL (" + oss.str() + ")";
  std::string test_sql_filename =           TEST_DATA_DIR + oss.str() + ".sql";
  std::string test_catalog_state_filename = TEST_DATA_DIR + oss.str() + ".xml";
  std::string res_catalog_state_filename =  TEST_DATA_DIR + oss.str() + "_res.xml";

  /* use initialized grt */
  GRT* grt= rdbms.get_grt();

  /* create & init new catalog */
  db_mysql_CatalogRef res_catalog(grt);
  res_catalog->version(rdbms->version());
  res_catalog->defaultCharacterSetName("utf8");
  res_catalog->defaultCollationName("utf8_general_ci");
  grt::replace_contents(res_catalog->simpleDatatypes(), rdbms->simpleDatatypes());

  /* parse sql */
  sql_facade->parseSqlScriptFileEx(res_catalog, test_sql_filename, options);

  /* rename schema if asked */
  if (old_schema_name && new_schema_name)
    sql_facade->renameSchemaReferences(res_catalog, old_schema_name, new_schema_name);

  /* serialization */
  grt->serialize(res_catalog, res_catalog_state_filename);

  /* unserialization */
  db_CatalogRef test_catalog= db_mysql_CatalogRef::cast_from(ValueRef(grt->unserialize(test_catalog_state_filename)));

  /* comparison */
  grt_ensure_equals(test_message.c_str(), res_catalog, test_catalog);
}


/* TABLE */
TEST_FUNCTION(2) { test_import_sql(0); }
TEST_FUNCTION(3) { test_import_sql(1); }
TEST_FUNCTION(4) { test_import_sql(2); }
TEST_FUNCTION(5) { test_import_sql(3); }
TEST_FUNCTION(6) { test_import_sql(4); }
TEST_FUNCTION(7) { test_import_sql(5); }
TEST_FUNCTION(8) { test_import_sql(6); }
TEST_FUNCTION(9) { test_import_sql(7); }
TEST_FUNCTION(10) { test_import_sql(8); }
TEST_FUNCTION(11) { test_import_sql(9); }
TEST_FUNCTION(12) { test_import_sql(10); }
TEST_FUNCTION(13) { test_import_sql(11); }
TEST_FUNCTION(14) { test_import_sql(12); }
TEST_FUNCTION(15) { test_import_sql(13); }
TEST_FUNCTION(16) { test_import_sql(14); }
TEST_FUNCTION(17) { test_import_sql(15); }
TEST_FUNCTION(18) { test_import_sql(16); }
TEST_FUNCTION(19) { test_import_sql(17); }
TEST_FUNCTION(20) { test_import_sql(18); }


/* INDEX */
TEST_FUNCTION(30) { test_import_sql(50); }
TEST_FUNCTION(31) { test_import_sql(51); }


/* VIEW */
TEST_FUNCTION(35) { test_import_sql(100); }
TEST_FUNCTION(36) { test_import_sql(101); }


/* ROUTINE */
TEST_FUNCTION(40) { test_import_sql(150); }
TEST_FUNCTION(41) { test_import_sql(151); }


/* TRIGGER */
TEST_FUNCTION(45) { test_import_sql(200); }


/* EVENT */
//!TEST_FUNCTION(50) { test_import_sql(250); }
//!TEST_FUNCTION(51) { test_import_sql(251); }
//!TEST_FUNCTION(52) { test_import_sql(252); }
//!TEST_FUNCTION(53) { test_import_sql(253); }


/* LOGFILE GROUP, TABLESPACE */
TEST_FUNCTION(55) { test_import_sql(300); }


/* SERVER LINK */
TEST_FUNCTION(60) { test_import_sql(350); }


/* ALTER STATEMENTS */
TEST_FUNCTION(61) { test_import_sql(400); }


/* DROP STATEMENTS */
TEST_FUNCTION(62) { test_import_sql(450); }


/* MISC */
TEST_FUNCTION(65) { test_import_sql(600); } // re-use of stub tables & columns


/* REAL-WORLD SCHEMATA (MANY OBJECTS) */
TEST_FUNCTION(70) { test_import_sql(700); } // sakila-db: schema structures (except of triggers)
TEST_FUNCTION(71) { test_import_sql(701); } // sakila-db: inserts & triggers
TEST_FUNCTION(72) { test_import_sql(702); } // sakila-db: mysqldump file
TEST_FUNCTION(73) { test_import_sql(703, "sakila", "new_schema_name"); } // sakila-db: mysqldump file


/* SCHEMA RENAME */
TEST_FUNCTION(80) { test_import_sql(900, "test", "new_schema_name"); }

//--------------------------------------------------------------------------------------------------

void check_fwd_engineer(WBTester &wbt, std::string &modelfile, std::string &expected_sql, std::map<std::string, bool> &fwd_opts)
{
  wbt.wb->open_document(modelfile);
  wbt.open_all_diagrams();
  wbt.activate_overview();

  if (!g_file_test(modelfile.c_str(),G_FILE_TEST_EXISTS))
  ensure("Model file not found!", false);

  DbMySQLSQLExport exp(wbt.wb->get_grt_manager(), db_mysql_CatalogRef::cast_from(wbt.get_catalog()));

  ValueRef valRef = wbt.wb->get_grt()->get("/wb/doc/physicalModels/0/catalog/schemata/0");

  db_mysql_SchemaRef schemaRef = db_mysql_SchemaRef::cast_from(valRef);
  ensure("Model not loaded :(", schemaRef.is_valid());

  bec::GrtStringListModel *users_model;
  bec::GrtStringListModel *users_imodel;
  bec::GrtStringListModel *tables_model;
  bec::GrtStringListModel *tables_imodel;
  bec::GrtStringListModel *views_model;
  bec::GrtStringListModel *views_imodel;
  bec::GrtStringListModel *routines_model;
  bec::GrtStringListModel *routines_imodel;
  bec::GrtStringListModel *triggers_model;
  bec::GrtStringListModel *triggers_imodel;

  exp.setup_grt_string_list_models_from_catalog(&users_model, &users_imodel,
                                                &tables_model, &tables_imodel,
                                                &views_model, &views_imodel,
                                                &routines_model, &routines_imodel,
                                                &triggers_model, &triggers_imodel);

  std::map<std::string, bool>::iterator it;
  for (it = fwd_opts.begin(); it != fwd_opts.end(); ++it)
    exp.set_option(it->first, it->second);

  exp.start_export(true);

  std::string output = exp.export_sql_script();

  std::ifstream ref(expected_sql.c_str());
  std::stringstream ss(output);

  std::string line, refline;

  tut::ensure(expected_sql, ref.is_open());

  std::string error_msg("Forward engineer of:");
  error_msg += modelfile;
  error_msg += " and ";
  error_msg += expected_sql;
  error_msg += " failed";

  int l = 0;
  while (ref.good() && ss.good())
  {
    ++l;
    getline(ref, refline);
    getline(ss, line);
    tut::ensure_equals(error_msg + base::strfmt(":%i", l), line, refline);
  }

  wbt.wb->close_document();
  wbt.wb->close_document_finish();
}

TEST_FUNCTION(90)
{
  //general test for forward engineer of sakilla database
  std::map<std::string, bool> opts;
  std::string modelfile = "data/forward_engineer/sakila.mwb";
  std::string expected_sql = "data/forward_engineer/sakila.expected.sql";


  opts.insert(std::make_pair("GenerateDrops", 1));
  opts.insert(std::make_pair("GenerateSchemaDrops", 1));
  opts.insert(std::make_pair("SkipForeignKeys", 1));
  opts.insert(std::make_pair("SkipFKIndexes", 1));
  opts.insert(std::make_pair("GenerateWarnings", 1));
  opts.insert(std::make_pair("GenerateCreateIndex", 1));
  opts.insert(std::make_pair("NoUsersJustPrivileges", 0));
  opts.insert(std::make_pair("NoViewPlaceholders", 0));
  opts.insert(std::make_pair("GenerateInserts", 0));
  opts.insert(std::make_pair("NoFKForInserts", 0));
  opts.insert(std::make_pair("TriggersAfterInserts", 1));
  opts.insert(std::make_pair("OmitSchemata", 0));
  opts.insert(std::make_pair("GenerateUse", 1));

  opts.insert(std::make_pair("TablesAreSelected", 1));
  opts.insert(std::make_pair("TriggersAreSelected", 1));
  opts.insert(std::make_pair("RoutinesAreSelected", 1));
  opts.insert(std::make_pair("ViewsAreSelected", 1));
  opts.insert(std::make_pair("UsersAreSelected", 1));

  check_fwd_engineer(wbt, modelfile, expected_sql, opts);

}

TEST_FUNCTION(91)
{
  //foward engineer test of routine with ommitSchemata enabled
  std::map<std::string, bool> opts;
  std::string modelfile = "data/forward_engineer/ommit_schema_routine.mwb";
  std::string expected_sql = "data/forward_engineer/ommit_schema_routine.expected.sql";


  opts.insert(std::make_pair("GenerateDrops", true));
  opts.insert(std::make_pair("GenerateSchemaDrops", false));
  opts.insert(std::make_pair("SkipForeignKeys", true));
  opts.insert(std::make_pair("SkipFKIndexes", false));
  opts.insert(std::make_pair("GenerateWarnings", false));
  opts.insert(std::make_pair("GenerateCreateIndex", false));
  opts.insert(std::make_pair("NoUsersJustPrivileges", false));
  opts.insert(std::make_pair("NoViewPlaceholders", false));
  opts.insert(std::make_pair("GenerateInserts", false));
  opts.insert(std::make_pair("NoFKForInserts", false));
  opts.insert(std::make_pair("TriggersAfterInserts", false));
  opts.insert(std::make_pair("OmitSchemata", true));
  opts.insert(std::make_pair("GenerateUse", false));

  opts.insert(std::make_pair("TablesAreSelected", true));
  opts.insert(std::make_pair("TriggersAreSelected", false));
  opts.insert(std::make_pair("RoutinesAreSelected", true));
  opts.insert(std::make_pair("ViewsAreSelected", false));
  opts.insert(std::make_pair("UsersAreSelected", true));

  check_fwd_engineer(wbt, modelfile, expected_sql, opts);

}

TEST_FUNCTION(92)
{
  //foward engineer test of routine with ommitSchemata enabled
  std::map<std::string, bool> opts;
  std::string modelfile = "data/forward_engineer/schema_rename.mwb";
  std::string expected_sql = "data/forward_engineer/schema_rename.expected.sql";


  opts.insert(std::make_pair("GenerateDrops", 1));
  opts.insert(std::make_pair("GenerateSchemaDrops", 1));
  opts.insert(std::make_pair("SkipForeignKeys", 1));
  opts.insert(std::make_pair("SkipFKIndexes", 1));
  opts.insert(std::make_pair("GenerateWarnings", 1));
  opts.insert(std::make_pair("GenerateCreateIndex", 1));
  opts.insert(std::make_pair("NoUsersJustPrivileges", 1));
  opts.insert(std::make_pair("NoViewPlaceholders", 1));
  opts.insert(std::make_pair("GenerateInserts", 1));
  opts.insert(std::make_pair("NoFKForInserts", 1));
  opts.insert(std::make_pair("TriggersAfterInserts", 1));
  opts.insert(std::make_pair("OmitSchemata", 1));
  opts.insert(std::make_pair("GenerateUse", 1));

  opts.insert(std::make_pair("TablesAreSelected", 1));
  opts.insert(std::make_pair("TriggersAreSelected", 1));
  opts.insert(std::make_pair("RoutinesAreSelected", 1));
  opts.insert(std::make_pair("ViewsAreSelected", 1));
  opts.insert(std::make_pair("UsersAreSelected", 1));

  check_fwd_engineer(wbt, modelfile, expected_sql, opts);

}


END_TESTS
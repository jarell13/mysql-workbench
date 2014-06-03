/* 
 * Copyright (c) 2007, 2014, Oracle and/or its affiliates. All rights reserved.
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

#include "recordset_cdbc_storage.h"
#include "recordset_be.h"
#include "sqlide_generics_private.h"
#include "sqlide_generics.h"
#include "grtsqlparser/sql_facade.h"
#include "base/string_utilities.h"
#include <sqlite/query.hpp>
#include <boost/cstdint.hpp>
#include <boost/foreach.hpp>
#include <algorithm>
#include <ctype.h>


using namespace bec;
using namespace grt;
using namespace base;


Recordset_cdbc_storage::Recordset_cdbc_storage(GRTManager *grtm)
:
Recordset_sql_storage(grtm),
_reloadable(true),
_gather_field_info(false)
{
}


Recordset_cdbc_storage::~Recordset_cdbc_storage()
{
}


sql::Dbc_connection_handler::ConnectionRef Recordset_cdbc_storage::dbms_conn_ref()
{
  if (!_dbms_conn || !_dbms_conn->ref.get_ptr())
    throw std::runtime_error("No connection to DBMS");
  return _dbms_conn->ref;
}

sql::Dbc_connection_handler::ConnectionRef Recordset_cdbc_storage::aux_dbms_conn_ref()
{
  if (!_aux_dbms_conn || !_aux_dbms_conn->ref.get_ptr())
  throw std::runtime_error("No connection to DBMS");
  return _aux_dbms_conn->ref;
}

class FetchVar : public boost::static_visitor<sqlite::variant_t>
{
public:
  FetchVar(sql::ResultSet *rs) : _rs(rs), _foreknown_blob_size(-1) {}
  result_type operator()(const sqlite::unknown_t &v, const sqlite::variant_t &index) const { return _rs->getString(boost::get<int>(index)); }
  result_type operator()(const sqlite::null_t &v, const sqlite::variant_t &index) const { return sqlite::null_t(); }
  result_type operator()(const std::string &v, const sqlite::variant_t &index) const { return _rs->getString(boost::get<int>(index)); }
  result_type operator()(const int &v, const sqlite::variant_t &index) const { return _rs->getInt(boost::get<int>(index)); }
  result_type operator()(const boost::int64_t &v, const sqlite::variant_t &index) const { return (boost::int64_t)_rs->getInt64(boost::get<int>(index)); }
  result_type operator()(const long double &v, const sqlite::variant_t &index) const { return _rs->getDouble(boost::get<int>(index)); }
  result_type operator()(const sqlite::blob_ref_t &v, const sqlite::variant_t &index)
  {
    sqlite::blob_ref_t blob_ref;
    std::auto_ptr<std::istream> is(_rs->getBlob(boost::get<int>(index)));
    if ((size_t)-1 == _foreknown_blob_size)
    {
      const size_t BUFF_SIZE= 4096;
      std::list<std::vector<char> > chunks;
      std::streamsize blob_size= 0;
      while (!is->eof())
      {
        chunks.resize(chunks.size()+1);
        std::vector<char> &chunk= *chunks.rbegin();
        chunk.resize(BUFF_SIZE);
        is->read(&chunk[0], BUFF_SIZE);
        blob_size+= is->gcount();
      }
      blob_ref.reset(new sqlite::blob_t(chunks.size() * BUFF_SIZE));
      sqlite::blob_t *blob= blob_ref.get();
      int n= 0;
      BOOST_FOREACH (const std::vector<char> &chunk, chunks)
      {
        memcpy(&(*blob)[n * BUFF_SIZE], &chunk[0], BUFF_SIZE);
        ++n;
      }
      blob->resize((size_t) blob_size); // TODO: make this compatible to streamsize.
    }
    else
    {
      blob_ref.reset(new sqlite::blob_t(_foreknown_blob_size));
      sqlite::blob_t *blob= blob_ref.get();
      is->read((char*)&(*blob)[0], _foreknown_blob_size);
      if ((size_t)is->gcount() != _foreknown_blob_size)
        throw std::runtime_error(strfmt("BLOB size mismatch: server reports %i bytes, fetched %i bytes", (int)_foreknown_blob_size, (int)is->gcount()));
      _foreknown_blob_size= -1;
    }
    return blob_ref;
  }
public:
  void foreknown_blob_size(size_t val) { _foreknown_blob_size= val; }
private:
  sql::ResultSet *_rs;
  size_t _foreknown_blob_size;
};


void Recordset_cdbc_storage::do_unserialize(Recordset *recordset, sqlite::connection *data_swap_db)
{
  sql::Dbc_connection_handler::ConnectionRef dbms_conn_ref= this->dbms_conn_ref();
  sql::Connection *dbms_conn= dbms_conn_ref.get();

  Recordset_sql_storage::do_unserialize(recordset, data_swap_db);

  std::string sql_query= decorated_sql_query();

  Recordset::Column_names &column_names= get_column_names(recordset);
  Recordset::Column_types &column_types= get_column_types(recordset);
  Recordset::Column_types &real_column_types= get_real_column_types(recordset);
  Recordset::Column_quoting &column_quoting= get_column_quoting(recordset);
  
  boost::shared_ptr<sql::Statement> stmt;
  boost::shared_ptr<sql::ResultSet> rs;
  if (_dbc_resultset)
  {
    rs= _dbc_resultset;
    _dbc_resultset.reset(); // handover memory management to scope shared_ptr because resultset can be read 1 time only
    // same about statement
    stmt= _dbc_statement;
    _dbc_statement.reset();
  }
  else
  {
    if (!_reloadable)
      throw std::runtime_error("Recordset can't be reloaded, original statement must be reexecuted instead");
    stmt.reset(dbms_conn->createStatement());
    //if (!_schema_name.empty()) //! default schema is to be set for connector
    //  stmt->execute(strfmt("use `%s`", _schema_name.c_str()));
    //stmt->setFetchSize(100); //! setFetchSize is not implemented. param value to be customized.
    stmt->execute(sql_query);
    rs.reset(stmt->getResultSet());
  }

  _valid= (NULL != rs.get());
  if (!_valid)
    return;

  sql::ResultSetMetaData *rs_meta(rs->getMetaData());

  ColumnId editable_col_count= rs_meta->getColumnCount();

  // column types
  static std::map<std::string, sqlite::variant_t> known_types;
  static std::map<std::string, sqlite::variant_t> known_real_types;
  {
    struct Known_type_initializer
    {
      //! these types are mysql specific
      //! TODO: int ResultSetMetaData::getColumnType must be used instead
      //! TODO: unify value range constraints
      Known_type_initializer(GRT *grt)
      {
        known_types["BIT"]= sqlite::unknown_t();

        known_types["ENUM"]= std::string();
        known_types["SET"]= std::string();

        known_types["DECIMAL"]= std::string();//!ld;

        known_types["TINYINT"]= std::string();//!int();
        known_types["SMALLINT"]= std::string();//!int();
        known_types["INT"]= std::string();//!int();
        known_types["MEDIUMINT"]= std::string();//!int();
        known_types["BIGINT"]= std::string();//!boost::int64_t();

        known_types["FLOAT"]= std::string();//!ld;
        known_types["DOUBLE"]= std::string();//!ld;

        known_types["NULL"]= sqlite::null_t();

        known_types["TIMESTAMP"]= std::string();
        known_types["DATE"]= std::string();
        known_types["TIME"]= std::string();
        known_types["DATETIME"]= std::string();
        known_types["YEAR"]= int();

        known_types["TINYBLOB"]= sqlite::blob_ref_t();
        known_types["BLOB"]= sqlite::blob_ref_t();
        known_types["MEDIUMBLOB"]= sqlite::blob_ref_t();
        known_types["LONGBLOB"]= sqlite::blob_ref_t();

        known_types["TINYTEXT"]= std::string();
        known_types["TEXT"]= std::string();
        known_types["MEDIUMTEXT"]= std::string();
        known_types["LONGTEXT"]= std::string();

        known_types["VARCHAR"]= std::string();
        known_types["CHAR"]= std::string();

        known_types["GEOMETRY"]= sqlite::unknown_t();

        known_types["UNKNOWN"]= sqlite::unknown_t();

        known_real_types= known_types;
        {
          long double ld;
          known_real_types["DECIMAL"]= ld;

          known_real_types["TINYINT"]= int();
          known_real_types["SMALLINT"]= int();
          known_real_types["INT"]= int();
          known_real_types["MEDIUMINT"]= int();
          known_real_types["BIGINT"]= boost::int64_t();

          known_real_types["FLOAT"]= ld;
          known_real_types["DOUBLE"]= ld;
          
          known_real_types["VARBINARY"]= sqlite::blob_ref_t();
          known_real_types["BINARY"]= sqlite::blob_ref_t();
        }
      }
    };
    static Known_type_initializer known_type_initializer(_grtm->get_grt());

    // assign these here since they can change
    known_types["VARBINARY"]= sqlite::blob_ref_t();
    known_types["BINARY"]= sqlite::blob_ref_t();
    DictRef options= DictRef::cast_from(_grtm->get_grt()->get("/wb/options/options"));
    if (options.is_valid())
    {
      bool treat_binary_as_text= (options.get_int("DbSqlEditor:MySQL:TreatBinaryAsText", 0) != 0);
      if (treat_binary_as_text)
      {
        known_types["VARBINARY"]= std::string();
        known_types["BINARY"]= std::string();
      }
    }
  }

  if (_gather_field_info)
  {
    for (unsigned int i = 0; i < (unsigned int)editable_col_count; ++i)
    {
      FieldInfo info;
      info.catalog = rs_meta->getCatalogName(i+1);
      info.schema = rs_meta->getSchemaName(i+1);
      info.table = rs_meta->getTableName(i+1);
      info.field = rs_meta->getColumnLabel(i+1);
      info.type = rs_meta->getColumnTypeName(i+1);
      info.display_size = rs_meta->getColumnDisplaySize(i+1);
      info.precision = rs_meta->getPrecision(i+1);
      info.scale = rs_meta->getScale(i+1);
      _field_info.push_back(info);
    }
      
    // Turns the flag off to prevent unnecesary
    // reprocessing to get the field info.
    _gather_field_info = false;
  }

  column_types.reserve(editable_col_count);
  // some column types might be defined in derived class. don't redefine types for those columns.
  for (unsigned int n = (unsigned int)column_types.size(); (unsigned int)editable_col_count > n; ++n)
  {
    std::string type_name= rs_meta->getColumnTypeName(n+1);
    type_name = base::toupper(type_name);
    std::string::size_type tne= type_name.find(' ');
    type_name= type_name.substr(0, tne);
    column_types.push_back(known_types[type_name]);
    real_column_types.push_back(known_real_types[type_name]);
    column_quoting.push_back(!rs_meta->isNumeric(n+1) && (sql::DataType::DECIMAL != rs_meta->getColumnType(n+1)));
  }

  // column names
  column_names.reserve(editable_col_count);
  // some column names might be defined in derived class. don't redefine names for those columns.
  for (unsigned int n = (unsigned int)column_names.size(); (unsigned int)editable_col_count > n; ++n)
    column_names.push_back(rs_meta->getColumnLabel(n+1));

  // determine pkey or unique identifier columns
  ColumnId rowid_col_count= 0;
  if (!_table_name.empty()) // we need PK info only for editable statements and table_name member is filled only for those
  {
    // a connection other than the user connection must be used for fetching metadata, otherwise we change the state of the connection
    sql::Dbc_connection_handler::ConnectionRef aux_dbms_conn_ref= this->aux_dbms_conn_ref();

    sql::DatabaseMetaData *conn_meta(aux_dbms_conn_ref->getMetaData());
    std::auto_ptr<sql::ResultSet> rs(conn_meta->getBestRowIdentifier("", _schema_name, _table_name, 0, 0));
    rowid_col_count= rs->rowsCount();
    if (rowid_col_count > 0)
    {
      _pkey_columns.reserve(rowid_col_count);
      column_names.reserve(editable_col_count + rowid_col_count);
      column_types.reserve(editable_col_count + rowid_col_count);
      real_column_types.reserve(editable_col_count + rowid_col_count);
      while (rs->next())
      {
        Recordset::Column_names::const_iterator i=
          std::find(column_names.begin(), column_names.end(), rs->getString("COLUMN_NAME"));
        if (i != column_names.end())
        {
          ColumnId col= std::distance((Recordset::Column_names::const_iterator)column_names.begin(), i);
          column_names.push_back(column_names[col]);
          column_types.push_back(column_types[col]);
          real_column_types.push_back(real_column_types[col]);
          _pkey_columns.push_back(col); // copy original value of pk field(s)
        }
        else
          rowid_col_count--;
      }
    }
    else
    {
      _readonly = true;
      _readonly_reason = "The table has no unique row identifier (primary key or a NOT NULL unique index)";
    }
  }

  // columns values of that must be null to signify that actual value to be fetched on-demand (e.g. when open blob editor)
  std::vector<bool> null_value_columns(editable_col_count);
  {
    bool are_null_columns_possible= recordset->optimized_blob_fetching() && _reloadable && rowid_col_count;
    for (ColumnId col= 0; editable_col_count > col; ++col)
      null_value_columns[col]= are_null_columns_possible && sqlide::is_var_blob(real_column_types[col]);
  }

  // data
  {
    sqlide::Sqlite_transaction_guarder transaction_guarder(data_swap_db, false);

    create_data_swap_tables(data_swap_db, column_names, column_types);

    ColumnId col_count= editable_col_count + rowid_col_count;
    FetchVar fetch_var(rs.get());
    Var_vector row_values(col_count);

    std::list<boost::shared_ptr<sqlite::command> > insert_commands= prepare_data_swap_record_add_statement(data_swap_db, column_names);

    while (rs->next())
    {
      for (ColumnId n= 0; editable_col_count > n; ++n)
      {
        if (rs->isNull((int)n + 1) || null_value_columns[n])
        {
          row_values[n]= sqlite::null_t();
        }
        else
        {
          sqlite::variant_t index= (int)n+1;
          row_values[n]= boost::apply_visitor(fetch_var, column_types[n], index);
        }
      }
      for (ColumnId n= 0; rowid_col_count > n; ++n) // copy original value of pk field(s)
        row_values[editable_col_count+n]= row_values[_pkey_columns[n]];
      add_data_swap_record(insert_commands, row_values);

      if (_dbms_conn->is_stop_query_requested)
        throw std::runtime_error(_("Query execution has been stopped, the connection to the DB server was not restarted, any open transaction remains open"));
    }

    transaction_guarder.commit();
  }

  // remap rowid columns to duplicated columns
  for (ColumnId rowid_col= 0, col= editable_col_count; rowid_col_count > rowid_col; ++col, ++rowid_col)
    _pkey_columns[rowid_col]= col;
}


void Recordset_cdbc_storage::do_fetch_blob_value(Recordset *recordset, sqlite::connection *data_swap_db, RowId rowid, ColumnId column, sqlite::variant_t &blob_value)
{
  sql::Dbc_connection_handler::ConnectionRef dbms_conn= dbms_conn_ref();

  Recordset::Column_names &column_names= get_column_names(recordset);
  Recordset::Column_types &column_types= get_column_types(recordset);

  if (column >= column_names.size())
    return;

  std::string sql_query= decorated_sql_query();
  {
    std::string pkey_predicate;
    get_pkey_predicate_for_data_cache_rowid(recordset, data_swap_db, rowid, pkey_predicate);
    if (pkey_predicate.empty())
    {
      // no pk for recordset - blob fields are fetched along with other fields - hence this is a true NULL value rather than a placeholder
      blob_value= sqlite::null_t();
      return;
    }
    sql_query= strfmt("select `%s`, length(`%s`) from (%s) t where %s",
      column_names[column].c_str(), column_names[column].c_str(), sql_query.c_str(), pkey_predicate.c_str());
  }

  if (!_reloadable)
    throw std::runtime_error("Recordset can't be reloaded, original statement must be reexecuted instead");
  boost::shared_ptr<sql::Statement> stmt(dbms_conn->createStatement());
  stmt->execute(sql_query);
  boost::shared_ptr<sql::ResultSet> rs(stmt->getResultSet());

  _valid= (NULL != rs.get());
  if (!_valid)
    return;

  {
    FetchVar fetch_var(rs.get());
    while (rs->next())
    {
      sqlite::variant_t val;
      if (rs->isNull(1))
      {
        blob_value= sqlite::null_t();
      }
      else
      {
        fetch_var.foreknown_blob_size(rs->getInt(2));
        sqlite::variant_t index= 1;
        blob_value= boost::apply_visitor(fetch_var, column_types[column], index);
      }
    }
  }
}


class BlobVarToStream : public boost::static_visitor<boost::shared_ptr<std::stringstream> >
{
public:
  result_type operator()(const sqlite::blob_ref_t &v)
  {
    size_t data_length= v->size();
    std::string data((const char*)&(*v)[0], data_length);
    return result_type(new std::stringstream(data));
  }
  result_type operator()(const std::string &v) { return result_type(new std::stringstream(v)); }
  template<typename V> result_type operator()(const V &t) { return result_type(new std::stringstream()); }
};
void Recordset_cdbc_storage::run_sql_script(const Sql_script &sql_script)
{
  sql::Dbc_connection_handler::ConnectionRef dbms_conn= dbms_conn_ref();

  float progress_state= 0.f;
  float progress_state_inc= sql_script.statements.empty() ? 1.f : 1.f / sql_script.statements.size();
  int err_count= 0;
  int processed_statement_count= 0;
  std::string msg;
  BlobVarToStream blob_var_to_stream;
  Sql_script::Statements_bindings::const_iterator sql_bindings= sql_script.statements_bindings.begin();
  std::auto_ptr<sql::PreparedStatement> stmt;
  BOOST_FOREACH (const std::string &sql, sql_script.statements)
  {
    try
    {
      stmt.reset(dbms_conn->prepareStatement(sql));
      std::list<boost::shared_ptr<std::stringstream> > blob_streams;
      if (sql_script.statements_bindings.end() != sql_bindings)
      {
        int bind_var_index= 1;
        BOOST_FOREACH (const sqlite::variant_t &bind_var, *sql_bindings)
        {
          if (sqlide::is_var_null(bind_var))
          {
            stmt->setNull(bind_var_index, 0);
          }
          else
          {
            boost::shared_ptr<std::stringstream> blob_stream= boost::apply_visitor(blob_var_to_stream, bind_var);
            if (binding_blobs())
            {
              blob_streams.push_back(blob_stream);
              stmt->setBlob(bind_var_index, blob_stream.get());
            }
          }
          ++bind_var_index;
        }
      }
      stmt->executeUpdate();
    }
    catch (sql::SQLException &e)
    {
      ++err_count;
      msg = strfmt("%i: %s", e.getErrorCode(), e.what());
      on_sql_script_run_error(e.getErrorCode(), msg, sql);
    }
    ++processed_statement_count;
    progress_state+= progress_state_inc;
    on_sql_script_run_progress(progress_state);
    ++sql_bindings;
  }
  if (err_count)
  {
    dbms_conn->rollback();
    msg= strfmt("%i error(s) saving changes to table %s", err_count, full_table_name().c_str());
    on_sql_script_run_statistics((processed_statement_count - err_count), err_count);
    throw std::runtime_error(msg.c_str());
  }
  else
  {
    dbms_conn->commit();
    on_sql_script_run_statistics((processed_statement_count - err_count), err_count);
  }
}


std::string Recordset_cdbc_storage::decorated_sql_query()
{
  std::string sql_query;
  if (!_sql_query.empty())
    sql_query= _sql_query;
  else
    sql_query= strfmt("select * from %s%s", full_table_name().c_str(), _additional_clauses.c_str());

  if (_limit_rows)
  {
    SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(_rdbms);
    Sql_specifics::Ref sql_specifics= sql_facade->sqlSpecifics();
    sql_query= sql_specifics->limit_select_query(sql_query, &_limit_rows_count, &_limit_rows_offset);
  }

  return sql_query;
}
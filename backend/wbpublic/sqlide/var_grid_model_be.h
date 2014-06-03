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

#ifndef _VAR_GRID_MODEL_BE_H_
#define _VAR_GRID_MODEL_BE_H_

#include "wbpublic_public_interface.h"
#include "sqlide_generics.h"
#include "grt/grt_threaded_task.h"
#include "grt/tree_model.h"
#include "grt/grt_manager.h"
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <vector>

class Recordset_data_storage;

namespace sqlite
{
  struct query;
  struct result;
}

class WBPUBLICBACKEND_PUBLIC_FUNC VarGridModel : public bec::GridModel, public boost::enable_shared_from_this<VarGridModel>
{
public:
  typedef boost::shared_ptr<VarGridModel> Ref;
  virtual ~VarGridModel();
protected:
  VarGridModel(bec::GRTManager *grtm);

  friend class Recordset_data_storage;

public:
  bec::GRTManager * grtm() const { return _grtm; }
protected:
  bec::GRTManager *_grtm;

public:
  virtual void reset();
  virtual void refresh() {}
  boost::function<void ()> rows_changed;
  boost::signals2::signal<void ()> refresh_ui_signal;

protected:
  boost::signals2::connection _refresh_connection;

  virtual int refresh_ui();

public:
  virtual size_t row_count() const { return _row_count; }
  virtual size_t count();
  virtual size_t get_column_count() const { return _column_count; }
  virtual std::string get_column_caption(ColumnId index);
  virtual ColumnType get_column_type(ColumnId column);
  virtual ColumnType get_real_column_type(ColumnId column);

public:
  virtual bool is_readonly() const { return _readonly; }
  virtual std::string readonly_reason() const { return _readonly_reason; }
protected:
  bool _readonly;
  std::string _readonly_reason;

public:
  typedef std::vector<std::string> Column_names;
  typedef std::vector<sqlite::variant_t> Column_types;
  typedef Data::const_iterator Cell_const;
  typedef std::vector<int> Column_quoting; // was bool, but it had some wierd effects where returned value was random

protected:
  typedef Data::iterator Cell;

public:
  virtual bec::IconId get_field_icon(const bec::NodeId &node, ColumnId column, bec::IconSize size);
private:
  class IconForVal;
  boost::scoped_ptr<IconForVal> _icon_for_val;
public:
  virtual bool set_field(const bec::NodeId &node, ColumnId column, const sqlite::variant_t &value);
  virtual bool set_field(const bec::NodeId &node, ColumnId column, const std::string &value);
  virtual bool set_field(const bec::NodeId &node, ColumnId column, double value);
  virtual bool set_field(const bec::NodeId &node, ColumnId column, ssize_t value);
  virtual bool get_field(const bec::NodeId &node, ColumnId column, std::string &value);
  virtual bool get_field_repr(const bec::NodeId &node, ColumnId column, std::string &value);
  bool get_field_repr_no_truncate(const bec::NodeId &node, ColumnId column, std::string &value);
  virtual bool get_field(const bec::NodeId &node, ColumnId column, ssize_t &value);
  virtual bool get_field(const bec::NodeId &node, ColumnId column, double &value);
  virtual bool get_field(const bec::NodeId &node, ColumnId column, sqlite::variant_t &value);
protected:
  bool get_field_(const bec::NodeId &node, ColumnId column, std::string &value);
  bool get_field_repr_(const bec::NodeId &node, ColumnId column, std::string &value);
  bool get_field_(const bec::NodeId &node, ColumnId column, ssize_t &value);
  bool get_field_(const bec::NodeId &node, ColumnId column, double &value);
  bool get_field_(const bec::NodeId &node, ColumnId column, sqlite::variant_t &value);
protected:
  virtual bool get_field_grt(const bec::NodeId &node, ColumnId column, grt::ValueRef &value);
  virtual void after_set_field(const bec::NodeId &node, ColumnId column, const sqlite::variant_t &value) {}

public:
  virtual bool is_field_null(const bec::NodeId &node, ColumnId column);
  virtual bool set_field_null(const bec::NodeId &node, ColumnId column);

public:
  virtual const Data & data() { return _data; }
protected:
  virtual bool get_cell(Cell &cell, const bec::NodeId &node, ColumnId column, bool allow_new_row);
  virtual Cell cell(RowId row, ColumnId column);
  void add_column(const std::string &name, const sqlite::variant_t &type);
protected:
  Data _data;
  RowId _row_count;
  ColumnId _column_count;
  Column_names _column_names;
  Column_types _column_types;
  Column_types _real_column_types; //! as a temp workaround for quick-fix of #38600: Insert statement calling function is incorrectly parsed
  Column_quoting _column_quoting; // stored whether column values are quoted or not (ie numbers vs strings. special values like functions need extra handling)
  //XXX maybe _real_column_types and _column_quoting can be merged somehow --alfredo
  
  base::RecMutex _data_mutex;

protected:
  boost::shared_ptr<sqlite::connection> data_swap_db() const;
private:
  boost::shared_ptr<sqlite::connection> create_data_swap_db_connection() const;
private:
  mutable boost::shared_ptr<sqlite::connection> _data_swap_db;
  std::string _data_swap_db_path;
public:
  static const int DATA_SWAP_DB_TABLE_MAX_COL_COUNT;
public:
  size_t data_swap_db_partition_count() const;
public:
  static size_t data_swap_db_partition_count(ColumnId column_count);
  static std::string data_swap_db_partition_suffix(size_t partition);
  static size_t data_swap_db_column_partition(ColumnId column); // returns partition number containing passed column
  static bec::ListModel::ColumnId translate_data_swap_db_column(ListModel::ColumnId column, size_t *partition= NULL); // returns column number relative to containing partition
  static void prepare_partition_queries(sqlite::connection *data_swap_db, const std::string &query_text_template, std::list<boost::shared_ptr<sqlite::query> > &queries);
  static bool emit_partition_queries(sqlite::connection *data_swap_db, std::list<boost::shared_ptr<sqlite::query> > &queries, std::vector<boost::shared_ptr<sqlite::result> > &results, const std::list<sqlite::variant_t> &bind_vars= std::list<sqlite::variant_t>());
  static void emit_partition_commands(sqlite::connection *data_swap_db, size_t partition_count, const std::string &command_text_template, const std::list<sqlite::variant_t> &bind_vars= std::list<sqlite::variant_t>());

protected:
  void cache_data_frame(RowId center_row, bool force_reload);
protected:
  RowId _data_frame_begin;
  RowId _data_frame_end;
  sqlide::VarCast _var_cast;

public:
  virtual int floating_point_visible_scale();
  const sqlide::VarToStr * var2str_convertor() const { return &_var_to_str; }
protected:
  sqlide::VarToStr _var_to_str;
  sqlide::VarToStr _var_to_str_repr; // supposed to be used only by UI part, set to do truncation of long text values
  sqlide::VarToInt _var_to_int;
  sqlide::VarToLongDouble _var_to_long_double;

public:
  virtual void set_edited_field(RowId row_index, ColumnId col_index);
  bool is_field_value_truncation_enabled(bool val);

  size_t edited_field_row() { return _edited_field_row; }
  size_t edited_field_column() { return _edited_field_col; }

  // called when the backend changes the current edited field row/column and the frontend must reselect
  boost::function<void ()> update_edited_field;
protected:
  bool _is_field_value_truncation_enabled;
  RowId _edited_field_row;
  ColumnId _edited_field_col;

public:
  bool optimized_blob_fetching() const { return _optimized_blob_fetching; }
private:
  bool _optimized_blob_fetching;
};


#endif /* _VAR_GRID_MODEL_BE_H_ */
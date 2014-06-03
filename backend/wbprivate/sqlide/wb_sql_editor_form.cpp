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

#include "wb_sql_editor_form.h"

#include "grtdb/db_helpers.h"
#include "grtsqlparser/sql_facade.h"
#include "grtui/confirm_save_dialog.h"
#include "grtdb/editor_dbobject.h"
#include "grtdb/db_object_helpers.h"

#include "sqlide/recordset_be.h"
#include "sqlide/recordset_cdbc_storage.h"
#include "sqlide/wb_sql_editor_snippets.h"
#include "sqlide/wb_sql_editor_result_panel.h"
#include "sqlide/wb_sql_editor_tree_controller.h"
#include "sqlide/sql_script_run_wizard.h"

#include "sqlide/autocomplete_object_name_cache.h"

#include "objimpl/db.query/db_query_Resultset.h"

#include "base/string_utilities.h"
#include "base/notifications.h"
#include "base/sqlstring.h"
#include "base/file_functions.h"
#include "base/file_utilities.h"
#include "base/log.h"
#include "base/boost_smart_ptr_helpers.h"
#include "base/util_functions.h"

#include "workbench/wb_command_ui.h"
#include "workbench/wb_context_names.h"

#include <mysql_connection.h>

#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/signals2/connection.hpp>
#include "grt/common.h"

#include "query_side_palette.h"

#include "mforms/menubar.h"
#include "mforms/hypertext.h" // needed for d-tor
#include "mforms/tabview.h" // needed for d-tor
#include "mforms/splitter.h" // needed for d-tor
#include "mforms/toolbar.h"
#include "mforms/code_editor.h"

#include <math.h>

using namespace bec;
using namespace grt;
using namespace wb;
using namespace base;

using boost::signals2::scoped_connection;

DEFAULT_LOG_DOMAIN("SqlEditor")

static const char *SQL_EXCEPTION_MSG_FORMAT= _("Error Code: %i\n%s");
static const char *EXCEPTION_MSG_FORMAT= _("Error: %s");

#define CATCH_SQL_EXCEPTION_AND_DISPATCH(statement, log_message_index, duration) \
catch (sql::SQLException &e)\
{\
  set_log_message(log_message_index, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), statement, duration);\
}

#define CATCH_EXCEPTION_AND_DISPATCH(statement) \
catch (std::exception &e)\
{\
  add_log_message(DbSqlEditorLog::ErrorMsg, strfmt(EXCEPTION_MSG_FORMAT, e.what()), statement, "");\
}

#define CATCH_ANY_EXCEPTION_AND_DISPATCH(statement) \
catch (sql::SQLException &e)\
{\
  add_log_message(DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), statement, "");\
}\
CATCH_EXCEPTION_AND_DISPATCH(statement)

#define CATCH_ANY_EXCEPTION_AND_DISPATCH_TO_DEFAULT_LOG(statement) \
catch (sql::SQLException &e)\
{\
  _grtm->get_grt()->send_error(strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), statement);\
}\
catch (std::exception &e)\
{\
  _grtm->get_grt()->send_error(strfmt(EXCEPTION_MSG_FORMAT, e.what()), statement);\
}


class Timer
{
public:
  Timer(bool run_immediately) : _is_running(false), _start_timestamp(0), _duration(0)
  {
    if (run_immediately)
      run();
  }
  void reset()
  {
    _is_running= false;
    _start_timestamp= 0;
    _duration= 0;
  }
  void run()
  {
    if (_is_running)
      return;
    _is_running= true;
    _start_timestamp= timestamp();
  }
  void stop()
  {
    if (!_is_running)
      return;
    _is_running= false;
    _duration+= timestamp() - _start_timestamp;
  }
  double duration()
  {
    return _is_running ? (_duration + timestamp() - _start_timestamp) : _duration;
  }
  std::string duration_formatted()
  {
    return strfmt(_("%.3f sec"), duration());
  }
private:
  bool _is_running;
  double _start_timestamp;
  double _duration;
};


SqlEditorForm::Ref SqlEditorForm::create(wb::WBContextSQLIDE *wbsql, const db_mgmt_ConnectionRef &conn)
{
  SqlEditorForm::Ref instance(new SqlEditorForm(wbsql, conn)); 
  
  if (conn.is_valid())
  {
    //instance->connect();
    //instance->finish_startup();
  }
  return instance;
}

void SqlEditorForm::report_connection_failure(const std::string &error, const db_mgmt_ConnectionRef &target)
{
  std::string message;
  log_error("SQL editor could not be connected: %s\n", error.c_str());
  mforms::App::get()->set_status_text(_("Could not connect to target database."));

  message = "Your connection attempt failed for user '%user%' from your host to server at %server%:%port%:\n  %error%\n"\
  "\n"\
  "Please:\n"\
  "1 Check that mysql is running on server %server%\n"\
  "2 Check that mysql is running on port %port% (note: 3306 is the default, but this can be changed)\n"\
  "3 Check the %user% has rights to connect to %server% from your address (mysql rights define what clients can connect to the server and from which machines) \n"\
  "4 Make sure you are both providing a password if needed and using the correct password for %server% connecting from the host address you're connecting from";

  message = bec::replace_string(message, "%user%", target->parameterValues().get_string("userName"));
  message = bec::replace_string(message, "%port%", target->parameterValues().get("port").repr());
  message = bec::replace_string(message, "%server%", target->parameterValues().get_string("hostName", "localhost"));
  message = bec::replace_string(message, "%error%", error);

  log_error("%s", (message + '\n').c_str());
  mforms::Utilities::show_error(_("Cannot Connect to Database Server"), message, _("Close"));
}


SqlEditorForm::SqlEditorForm(wb::WBContextSQLIDE *wbsql, const db_mgmt_ConnectionRef &conn)
  :
  _wbsql(wbsql),
  _grtm(wbsql->get_grt_manager()),
  _menu(NULL),  // Please use NULL where pointers are assigned, not 0, to avoid confusing the param with ctor flags or similar!
  _toolbar(NULL),
  _autosave_lock(NULL),
  _autosave_disabled(false),
  _loading_workspace(false),
  _cancel_connect(false),
  _sql_editors_serial(0),
  _scratch_editors_serial(0),
  _active_sql_editor_index(0),
  _updating_sql_editor(0),
  _keep_alive_thread(NULL),
  _connection(conn),
  _aux_dbc_conn(new sql::Dbc_connection_handler()),
  _usr_dbc_conn(new sql::Dbc_connection_handler()),
  _last_server_running_state(UnknownState),
  _auto_completion_cache(NULL),
  exec_sql_task(GrtThreadedTask::create(_grtm)),
  _is_running_query(false),
  _live_tree(SqlEditorTreeController::create(this)),
  _side_palette_host(NULL),
  _side_palette(NULL),
  _history(DbSqlEditorHistory::create(_grtm))
{
  _log = DbSqlEditorLog::create(this, _grtm, 500);

  NotificationCenter::get()->add_observer(this, "GNApplicationActivated");
  NotificationCenter::get()->add_observer(this, "GNMainFormChanged");
  NotificationCenter::get()->add_observer(this, "GNFormTitleDidChange");
  NotificationCenter::get()->add_observer(this, "GNColorsChanged");
  GRTNotificationCenter::get()->add_grt_observer(this, "GRNServerStateChanged");

  _dbc_auth = sql::Authentication::create(_connection, "");

  exec_sql_task->send_task_res_msg(false);
  exec_sql_task->msg_cb(boost::bind(&SqlEditorForm::add_log_message, this, _1, _2, _3, ""));

  _last_log_message_timestamp = timestamp();

  _progress_status_update_interval= (_grtm->get_app_option_int("DbSqlEditor:ProgressStatusUpdateInterval", 500))/(double)1000;

  int keep_alive_interval= _grtm->get_app_option_int("DbSqlEditor:KeepAliveInterval", 600);
  if (keep_alive_interval != 0)
  {
    _keep_alive_thread= TimerActionThread::create(boost::bind(&SqlEditorForm::send_message_keep_alive, this), keep_alive_interval*1000);
    _keep_alive_thread->on_exit.connect(boost::bind(&SqlEditorForm::reset_keep_alive_thread, this));
  }

  _lower_case_table_names = 0;

  _continue_on_error= (_grtm->get_app_option_int("DbSqlEditor:ContinueOnError", 0) != 0);

  // set initial autocommit mode value
  _usr_dbc_conn->autocommit_mode= (_grtm->get_app_option_int("DbSqlEditor:AutocommitMode", 1) != 0);
}

void SqlEditorForm::cancel_connect()
{
  _cancel_connect = true;
}

void SqlEditorForm::check_server_problems()
{
  //_lower_case_table_names
  std::string compile_os;
  if (_usr_dbc_conn && get_session_variable(_usr_dbc_conn->ref.get(), "version_compile_os", compile_os))
  {
    if ((_lower_case_table_names == 0 && (base::starts_with(compile_os, "Win") || base::starts_with(compile_os, "osx"))) ||
     (_lower_case_table_names == 2 && base::starts_with(compile_os, "Win")))
      mforms::Utilities::show_message_and_remember(_("Server Configuration Problems"),
          "A server configuration problem was detected.\nThe server is in a system that does not properly support the selected lower_case_table_names option value. Some problems may occur.\nPlease consult the MySQL server documentation.",
          _("OK"), "", "",  "SQLIDE::check_server_problems::lower_case_table_names", "");
  }
}

void SqlEditorForm::finish_startup()
{
  setup_side_palette();

  _live_tree->finish_init();

  //we moved this here, cause it needs schema_sidebar to be fully created,
  //due to some race conditions that occurs sometimes
  if (_grtm->get_app_option_int("DbSqlEditor:CodeCompletionEnabled") == 1 && connected())
    {
      std::string cache_dir = _grtm->get_user_datadir() + "/cache/";
      try
      {
        base::create_directory(cache_dir, 0700); // No-op if the folder already exists.
        _auto_completion_cache = new AutoCompleteCache(sanitize_file_name(get_session_name()),
          boost::bind(&SqlEditorForm::get_autocompletion_connection, this, _1), cache_dir,
          boost::bind(&SqlEditorForm::on_cache_action, this, _1));
        _auto_completion_cache->refresh_schema_cache(""); // Start fetching of schema names immediately.

      }
      catch (std::exception &e)
      {
        _auto_completion_cache = NULL;
        log_error("Could not create auto completion cache (%s).\n%s\n", cache_dir.c_str(), e.what());
      }
    }
    else
      log_debug("Code completion is disabled, so no name cache is created\n");

  if (_usr_dbc_conn && !_usr_dbc_conn->active_schema.empty())
    _live_tree->on_active_schema_change(_usr_dbc_conn->active_schema);

  _grtm->run_once_when_idle(this, boost::bind(&SqlEditorForm::update_menu_and_toolbar, this));

  this->check_server_problems();

  // refresh snippets again, in case the initial load from DB is pending for shared snippets
  _side_palette->refresh_snippets();

  GRTNotificationCenter::get()->send_grt("GRNSQLEditorOpened", wbsql()->get_grt_editor_object(this), grt::DictRef());
}

//-------------------------------------------------------------------------------------------------- 

/**
 * Returns the name for this WQE instance derived from the connection it uses.
 * Used for workspace and action log.
 */
std::string SqlEditorForm::get_session_name()
{
  std::string name = _connection->name();
  if (name.empty())
    name = _connection->hostIdentifier();
  return name;
}

//-------------------------------------------------------------------------------------------------- 

void SqlEditorForm::restore_last_workspace()
{
  std::string name = get_session_name();
  if (!name.empty())
    load_workspace(sanitize_file_name(name));

  if (_sql_editors.empty())
    new_sql_scratch_area(false);

  // Gets the title for a NEW editor
  _title = create_title();
  title_changed();  
}

void SqlEditorForm::title_changed()
{
  base::NotificationInfo info;
  info["form"] = form_id();
  info["title"] = _title;
  info["connection"] = _connection->name();
  base::NotificationCenter::get()->send("GNFormTitleDidChange", this, info);
}


SqlEditorForm::~SqlEditorForm()
{
  if (_auto_completion_cache)
    _auto_completion_cache->shutdown();
  
  {
    // Ensure all processing is stopped before freeing the info structure, otherwise references
    // are kept that prevent the correct deletion of the editor.
    MutexLock sql_editors_mutex(_sql_editors_mutex);
    BOOST_FOREACH (EditorInfo::Ref sql_editor_info, _sql_editors)
      sql_editor_info->editor->stop_processing();
  }

  NotificationCenter::get()->remove_observer(this);
  GRTNotificationCenter::get()->remove_grt_observer(this);

  delete _auto_completion_cache;
  delete _autosave_lock;
  _autosave_lock = 0;
  
  // Destructor can be called before the startup was finished.
  // On Windows the side palette is a child of the palette host and hence gets freed when we
  // free the host. On other platforms both are the same. In any case, double freeing it is
  // not a good idea.
  if (_side_palette_host != NULL)
    _side_palette_host->release();

  delete _toolbar;
  delete _menu;
  reset();

  reset_keep_alive_thread();
}


void SqlEditorForm::handle_grt_notification(const std::string &name, grt::ObjectRef sender, grt::DictRef info)
{
  if (name == "GRNServerStateChanged")
  {
    db_mgmt_ConnectionRef conn(db_mgmt_ConnectionRef::cast_from(info.get("connection")));

    ServerState new_state = (info.get_int("state") != 0 ? RunningState : PossiblyStoppedState);

    if (_last_server_running_state != new_state)
    {
      _last_server_running_state = new_state;
      if (new_state == RunningState && ping())
      {
        // if new state is running but we're already connected, don't do anything
        return;
      }
      // reconnect when idle, to avoid any deadlocks
      if (conn.is_valid() && conn == connection_descriptor())
        _grtm->run_once_when_idle(this, boost::bind(&WBContextSQLIDE::reconnect_editor, wbsql(), this));
    }
  }
}

void SqlEditorForm::handle_notification(const std::string &name, void *sender, base::NotificationInfo &info)
{
  if (name == "GNMainFormChanged")
  {
    if (_side_palette)
      _side_palette->close_popover();
    if (info["form"] == form_id())
      update_menu_and_toolbar();
  }
  else if (name == "GNFormTitleDidChange")
  {
    // Validates only if another editor to the same connection has sent the notification
    if (info["form"] != form_id() && _connection->name() == info["connection"])
    {
      // This code is reached when at least 2 editors to the same host
      // have been opened, so the label of the old editor (which may not
      // contain the schema name should be updated with it).
      update_title();
    }
  }
  else if (name == "GNColorsChanged")
  {
    // Single colors or the entire color scheme changed.
    update_toolbar_icons();
  }
  else if (name == "GNApplicationActivated")
  {
    check_external_file_changes();
  }
}


void SqlEditorForm::reset_keep_alive_thread()
{
  MutexLock keep_alive_thread_lock(_keep_alive_thread_mutex);
  if (_keep_alive_thread)
  {
    _keep_alive_thread->stop(true);
    _keep_alive_thread= NULL;
  }
}


void SqlEditorForm::jump_to_placeholder()
{
  int editor = active_sql_editor_index();
  if (editor >= 0)
    sql_editor(editor)->get_editor_control()->jump_to_next_placeholder();
}


grt::StringRef SqlEditorForm::do_disconnect(grt::GRT *grt)
{
  if (_usr_dbc_conn->ref.get())
  {
    {
      RecMutexLock lock(_usr_dbc_conn_mutex);
      close_connection(_usr_dbc_conn);
      _usr_dbc_conn->ref.reset();
    }

    {
      RecMutexLock lock(_aux_dbc_conn_mutex);
      close_connection(_aux_dbc_conn);
      _aux_dbc_conn->ref.reset();
    }
  }

  return grt::StringRef();
}

void SqlEditorForm::close()
{
  grt::ValueRef option(_grtm->get_app_option("workbench:SaveSQLWorkspaceOnClose"));
  if (option.is_valid() && *grt::IntegerRef::cast_from(option))
  {
    _grtm->replace_status_text("Saving workspace state...");
    if (_autosave_path.empty())
    {
      save_workspace(sanitize_file_name(get_session_name()), false);
      delete _autosave_lock;
    }
    else
    {
      auto_save();
        
      // Remove auto lock first or renaming the folder will fail.
      delete _autosave_lock;
      std::string new_name(base::strip_extension(_autosave_path)+".workspace");
      int try_count = 0;

      // Rename our temporary workspace if one exists to make it a persistent one.
      if (base::file_exists(_autosave_path))
      {
        for (;;)
        {
          try
          {
            if (base::file_exists(new_name))
              base::remove_recursive(new_name);
            base::rename(_autosave_path, new_name);
          }
          catch (base::file_error &err)
          {
            std::string path(dirname(_autosave_path));
            do
            {
              ++try_count;
              new_name = make_path(path, sanitize_file_name(get_session_name()).append(strfmt("-%i.workspace", try_count)));
            } while (file_exists(new_name));

            if (err.code() == base::already_exists)
              continue;
            log_warning("Could not rename autosave directory %s: %s\n", 
              _autosave_path.c_str(), err.what());
          }

          break;
        }
      }
    }
    _autosave_lock = 0;
  }
  else
  {
    delete _autosave_lock;
    _autosave_lock = 0;
    if (!_autosave_path.empty())
      base_rmdir_recursively(_autosave_path.c_str());
  }

  _grtm->replace_status_text("Closing SQL Editor...");
  wbsql()->editor_will_close(this);

  exec_sql_task->exec(true, boost::bind(&SqlEditorForm::do_disconnect, this, _1));
  exec_sql_task->disconnect_callbacks();
  reset_keep_alive_thread();
  _grtm->replace_status_text("SQL Editor closed");

  delete _menu;
  _menu = 0;
  delete _toolbar;
  _toolbar = 0;
}


std::string SqlEditorForm::get_form_context_name() const
{
  return WB_CONTEXT_QUERY;
}


bool SqlEditorForm::get_session_variable(sql::Connection *dbc_conn, const std::string &name, std::string &value)
{
  if (dbc_conn)
  {
    SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms());
    Sql_specifics::Ref sql_specifics= sql_facade->sqlSpecifics();
    std::string query= sql_specifics->query_variable(name);
    if (query.empty())
      return false;
    boost::scoped_ptr<sql::Statement> statement(dbc_conn->createStatement());
    boost::scoped_ptr<sql::ResultSet> rs(statement->executeQuery(query));
    if (rs->next())
    {
      value= rs->getString(2);
      return true;
    }
  }
  return false;
}


std::string SqlEditorForm::fetch_data_from_stored_procedure(std::string proc_call, boost::shared_ptr<sql::ResultSet> &rs)
{
  std::string ret_val("");
  try
  {
    RecMutexLock aux_dbc_conn_mutex(ensure_valid_aux_connection());
    std::auto_ptr<sql::Statement> stmt(_aux_dbc_conn->ref->createStatement());
    stmt->execute(std::string(proc_call));
    do
    {
      rs.reset(stmt->getResultSet());
    } while(stmt->getMoreResults());
  }
  catch (const sql::SQLException& exc)
  {
    log_warning("Error retrieving data from stored procedure '%s': Error %d : %s", proc_call.c_str(), exc.getErrorCode(), exc.what());
    ret_val = base::strfmt("MySQL Error : %s (code %d)", exc.what(), exc.getErrorCode());
  }  

  return ret_val;
}

void SqlEditorForm::cache_sql_mode()
{
  std::string sql_mode;
  if (_usr_dbc_conn && get_session_variable(_usr_dbc_conn->ref.get(), "sql_mode", sql_mode))
  {
    if (sql_mode != _sql_mode)
    {
      _sql_mode= sql_mode;
      MutexLock sql_editors_mutex(_sql_editors_mutex);
      BOOST_FOREACH (EditorInfo::Ref sql_editor_info, _sql_editors)
        sql_editor_info->editor->set_sql_mode(sql_mode);
    }
  }
}


void SqlEditorForm::query_ps_statistics(boost::int64_t conn_id, std::map<std::string, boost::int64_t> &stats)
{
  static const char *stat_fields[] = {
    "TIMER_WAIT",
    "LOCK_TIME",
    "ERRORS",
    "WARNINGS",
    "ROWS_AFFECTED",
    "ROWS_SENT",
    "ROWS_EXAMINED",
    "CREATED_TMP_DISK_TABLES",
    "CREATED_TMP_TABLES",
    "SELECT_FULL_JOIN",
    "SELECT_FULL_RANGE_JOIN",
    "SELECT_RANGE",
    "SELECT_RANGE_CHECK",
    "SELECT_SCAN",
    "SORT_MERGE_PASSES",
    "SORT_RANGE",
    "SORT_ROWS",
    "SORT_SCAN",
    "NO_INDEX_USED",
    "NO_GOOD_INDEX_USED",
    NULL
  };
  RecMutexLock lock(ensure_valid_aux_connection());

  std::auto_ptr<sql::Statement> stmt(_aux_dbc_conn->ref->createStatement());

  try
  {
    std::auto_ptr<sql::ResultSet> result(stmt->executeQuery(base::strfmt("SELECT st.* FROM performance_schema.events_statements_current st JOIN performance_schema.threads thr ON thr.thread_id = st.thread_id WHERE thr.processlist_id = %"PRId64, conn_id)));
    while (result->next())
    {

      for (const char **field = stat_fields; *field; ++field)
      {
        stats[*field] = result->getInt64(*field);
      }
    }
  }
  catch (sql::SQLException &exc)
  {
    log_exception("Error querying performance_schema.events_statements_current\n", exc);
  }
}


int SqlEditorForm::run_sql_in_scratch_tab(const std::string &sql, bool reuse_if_possible, bool start_collapsed)
{
  if (_active_sql_editor_index < 0 || !reuse_if_possible || !sql_editor_is_scratch(_active_sql_editor_index))
    new_sql_scratch_area(start_collapsed);
  set_sql_editor_text(sql.c_str());
  run_editor_contents(false);
  sql_editor(_active_sql_editor_index)->get_editor_control()->reset_dirty();
  
  return _active_sql_editor_index;
}

/**
 * Starts the auto completion list in the currently active editor. The content of this list is
 * determined from various sources + the current query context.
 */
void SqlEditorForm::list_members()
{
  MySQLEditor::Ref editor = active_sql_editor();
  if (editor)
    editor->show_auto_completion(true);
}

void SqlEditorForm::reset()
{
  MySQLEditor::Ref editor = active_sql_editor();
  if (editor)
    editor->cancel_auto_completion();
}


int SqlEditorForm::add_log_message(int msg_type, const std::string &msg, const std::string &context, const std::string &duration)
{
  RowId new_log_message_index= _log->add_message(msg_type, context, msg, duration);
  _has_pending_log_messages= true;
  refresh_log_messages(false);
  if (msg_type == DbSqlEditorLog::ErrorMsg || msg_type == DbSqlEditorLog::WarningMsg)
    _exec_sql_error_count++;
  return (int)new_log_message_index;
}


void SqlEditorForm::set_log_message(RowId log_message_index, int msg_type, const std::string &msg, const std::string &context, const std::string &duration)
{
  if (log_message_index != (RowId)-1)
  {
    _log->set_message(log_message_index, msg_type, context, msg, duration);
    _has_pending_log_messages= true;
    if (msg_type == DbSqlEditorLog::ErrorMsg || msg_type == DbSqlEditorLog::WarningMsg)
      _exec_sql_error_count++;
    refresh_log_messages(msg_type == DbSqlEditorLog::BusyMsg); // Force refresh only for busy messages.
  }
}


void SqlEditorForm::refresh_log_messages(bool ignore_last_message_timestamp)
{
  if (_has_pending_log_messages)
  {
    bool is_refresh_needed= ignore_last_message_timestamp;
    if (!ignore_last_message_timestamp)
    {
      double now= timestamp();
      if (_last_log_message_timestamp + _progress_status_update_interval < now)
        is_refresh_needed= true;
      _last_log_message_timestamp = now;
    }
    if (is_refresh_needed)
    {
      _log->refresh();
      _has_pending_log_messages= false;
    }
  }
}


int SqlEditorForm::recordset_count(int editor)
{
  if (editor >= 0 && editor < (int)_sql_editors.size())
    return (int)sql_editor_recordsets(editor)->size();
  return 0;
}

Recordset::Ref SqlEditorForm::recordset(int editor, int index)
{
  if (editor >= 0 && editor < (int)_sql_editors.size())
  {
    return _sql_editors[editor]->recordsets->at(index);
  }
  return Recordset::Ref();
}


boost::shared_ptr<SqlEditorResult> SqlEditorForm::result_panel(Recordset::Ref rset)
{
   RecordsetData *rdata = dynamic_cast<RecordsetData*>(rset->client_data());
   if (!rdata->result_panel)
      rdata->result_panel = SqlEditorResult::create(this, rset);
   return rdata->result_panel;
}


Recordset::Ref SqlEditorForm::recordset_for_key(int editor, long key)
{
  if (editor >= 0 && editor < (int)_sql_editors.size())
  {
    RecordsetsRef rsets = sql_editor_recordsets(editor);
    for (Recordsets::iterator rend = rsets->end(), rec = rsets->begin(); rec != rend; ++rec)
    {
      if ((*rec)->key() == key)
        return *rec;
    }
  }
  return Recordset::Ref();
}

void SqlEditorForm::init_connection(sql::Connection* dbc_conn_ref, const db_mgmt_ConnectionRef& connectionProperties, sql::Dbc_connection_handler::Ref& dbc_conn, bool user_connection)
{
  db_mgmt_RdbmsRef rdbms= db_mgmt_RdbmsRef::cast_from(_connection->driver()->owner());
  SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms);
  Sql_specifics::Ref sql_specifics= sql_facade->sqlSpecifics();

  // connection startup script
  {
    std::list<std::string> sql_script;
    {
      sql_specifics->get_connection_startup_script(sql_script);
      bool use_ansi_quotes= (connectionProperties->parameterValues().get_int("useAnsiQuotes", 0) != 0);
      if (use_ansi_quotes)
      {
        std::string sql= sql_specifics->setting_ansi_quotes();
        if (!sql.empty())
          sql_script.push_back(sql);
      }
    }
    
    // check if SQL_SAFE_UPDATES should be enabled (only for user connections, don't do it for the aux connection)
    if (_grtm->get_app_option_int("DbSqlEditor:SafeUpdates", 1) && user_connection)
      sql_script.push_back("SET SQL_SAFE_UPDATES=1");
    
    std::auto_ptr<sql::Statement> stmt(dbc_conn_ref->createStatement());
    sql::SqlBatchExec sql_batch_exec;
    sql_batch_exec(stmt.get(), sql_script);

    if (!user_connection)
    {
      std::string sql_mode;
      if (get_session_variable(dbc_conn_ref, "sql_mode", sql_mode)
          && sql_mode.find("MYSQL40") != std::string::npos)
      {
        // MYSQL40 used CREATE TABLE ... TYPE=<engine> instead of ENGINE=<engine>, which is not supported by our reveng code
        std::vector<std::string> options(base::split(sql_mode, ","));
        for (std::vector<std::string>::iterator i = options.begin(); i != options.end(); ++i)
        {
          if (*i == "MYSQL40")
          {
            options.erase(i);
            break;
          }
        }

        std::auto_ptr<sql::Statement> stmt(dbc_conn_ref->createStatement());
        std::string query = base::sqlstring("SET SESSION SQL_MODE=?", 0) << base::join(options, ",");
        stmt->execute(query);
      }
    }
  }

  // remember connection id
  {
    std::string query_connection_id= sql_specifics->query_connection_id();
    if (!query_connection_id.empty())
    {
      std::auto_ptr<sql::Statement> stmt(dbc_conn_ref->createStatement());
      stmt->execute(query_connection_id);
      boost::shared_ptr<sql::ResultSet> rs(stmt->getResultSet());
      rs->next();
      dbc_conn->id= rs->getInt(1);
    }
  }
}

static void set_active_schema(SqlEditorForm::Ptr self, const std::string &schema)
{
  SqlEditorForm::Ref ed(self.lock());
  if (ed)
    ed->active_schema(schema);
}

void SqlEditorForm::create_connection(sql::Dbc_connection_handler::Ref &dbc_conn, db_mgmt_ConnectionRef db_mgmt_conn, 
                                      boost::shared_ptr<sql::TunnelConnection> tunnel, sql::Authentication::Ref auth,
                                      bool autocommit_mode, bool user_connection)
{
  dbc_conn->is_stop_query_requested= false;

  sql::DriverManager *dbc_drv_man= sql::DriverManager::getDriverManager();

  db_mgmt_ConnectionRef temp_connection = db_mgmt_ConnectionRef::cast_from(grt::CopyContext(db_mgmt_conn.get_grt()).copy(db_mgmt_conn));

  int read_timeout = _grtm->get_app_option_int("DbSqlEditor:ReadTimeOut");
  if (read_timeout > 0)
    temp_connection->parameterValues().set("OPT_READ_TIMEOUT", grt::IntegerRef(read_timeout));
  temp_connection->parameterValues().set("CLIENT_INTERACTIVE", grt::IntegerRef(1));

  try
  {
    dbc_conn->ref= dbc_drv_man->getConnection(temp_connection, tunnel, auth,
                                              boost::bind(&SqlEditorForm::init_connection, this, _1, _2, dbc_conn, user_connection));

    note_connection_open_outcome(0); // succeess
  }
  catch (sql::SQLException &exc)
  {
    note_connection_open_outcome(exc.getErrorCode());
    throw;
  }

  //! dbms-specific code
  if (dbc_conn->ref->getMetaData()->getDatabaseMajorVersion() < 5)
  {
    throw std::runtime_error("MySQL Server version is older than 5.0, which is not supported");
  }

  // Activate default schema, if it's empty, use last active
  if (dbc_conn->active_schema.empty())
  {
    std::string default_schema = temp_connection->parameterValues().get_string("schema");

    if (default_schema.empty())
      default_schema = temp_connection->parameterValues().get_string("DbSqlEditor:LastDefaultSchema");
    if (!default_schema.empty())
    {
      try
      {
        dbc_conn->ref->setSchema(default_schema);
        dbc_conn->active_schema = default_schema;

        _grtm->run_once_when_idle(this, boost::bind(&set_active_schema, shared_from_this(), default_schema));
      }
      catch (std::exception &exc)
      {
        log_error("Can't restore DefaultSchema (%s): %s", default_schema.c_str(), exc.what());
        temp_connection->parameterValues().gset("DbSqlEditor:LastDefaultSchema", "");
      }
    }
  }
  else
    dbc_conn->ref->setSchema((dbc_conn->active_schema));

  dbc_conn->ref->setAutoCommit(autocommit_mode);
  dbc_conn->autocommit_mode= dbc_conn->ref->getAutoCommit();
}


struct ConnectionErrorInfo
{
  sql::AuthenticationError *auth_error;
  bool password_expired;
  bool server_probably_down;

  ConnectionErrorInfo() : auth_error(0), password_expired(false), server_probably_down(false) {}
  ~ConnectionErrorInfo()
  {
    delete auth_error;
  }
};


bool SqlEditorForm::connect(boost::shared_ptr<sql::TunnelConnection> tunnel)
{
  sql::Authentication::Ref auth = _dbc_auth;//sql::Authentication::create(_connection, "");
  enum PasswordMethod {
    NoPassword,
    KeychainPassword,
    InteractivePassword
  } current_method = NoPassword;

  reset();

  // In the 1st connection attempt, no password is supplied
  // If it fails, keychain is checked and used if it exists
  // If it fails, an interactive password request is made
  
  // connect
  for (;;)
  {
    // if an error happens in the worker thread, this ptr will be set
    ConnectionErrorInfo error_ptr;
    
    // connection must happen in the worker thread
    try
    {
      exec_sql_task->exec(true, boost::bind(&SqlEditorForm::do_connect, this, _1, tunnel, auth, &error_ptr));

      //check if user cancelled
      if (_cancel_connect) //return false, so it looks like the server is down
      {
        close();
        return false;
      }
    }
    catch (grt::grt_runtime_error)
    {
      if (error_ptr.password_expired)
        throw std::runtime_error(":PASSWORD_EXPIRED");

      if (!error_ptr.auth_error)
        throw;
      else if (error_ptr.server_probably_down)
        return false;

      //check if user cancelled
      if (_cancel_connect) //return false, so it looks like the server is down
      {
        close();
        return false;
      }

      if (current_method == NoPassword)
      {
        // lookup in keychain
        std::string pwd;
        if (sql::DriverManager::getDriverManager()->findStoredPassword(auth->connectionProperties(), pwd))
        {
          auth->set_password(pwd.c_str());
          current_method = KeychainPassword;
        }
        else
        {
          // not in keychain, go straight to interactive
          pwd = sql::DriverManager::getDriverManager()->requestPassword(auth->connectionProperties(), true);
          auth->set_password(pwd.c_str());
          current_method = InteractivePassword;
        }
      }
      else if (current_method == KeychainPassword)
      {
        // now try interactive
        std::string pwd = sql::DriverManager::getDriverManager()->requestPassword(auth->connectionProperties(), true);
        auth->set_password(pwd.c_str());  
      }
      else // if interactive failed, pass the exception higher up to be displayed to the user
        throw;
      continue;
    }
    break;
  }

  // we should only send this after the initial connection
  // assumes setup_side_palette() is called in finish_init(), signalizing that the editor was already initialized once
  if (_side_palette) // we're in a thread here, so make sure the notification is sent from the main thread
  {
    _grtm->run_once_when_idle(this, boost::bind(&SqlEditorForm::update_connected_state, this));
  }

  return true;
}

//--------------------------------------------------------------------------------------------------

void SqlEditorForm::update_connected_state()
{
  grt::DictRef args(_grtm->get_grt());
  args.gset("connected", connected());
  GRTNotificationCenter::get()->send_grt("GRNSQLEditorReconnected", wbsql()->get_grt_editor_object(this), args);

  update_menu_and_toolbar();
}

//--------------------------------------------------------------------------------------------------

/**
 * Little helper to create a single html line used for info output.
 */
std::string create_html_line(const std::string& name, const std::string& value)
{
  return "<div style=\"padding-left: 15px\"><span style=\"color: #717171\">" + name + "</span> <i>" +
    value + "</i></div>";
}

//--------------------------------------------------------------------------------------------------

grt::StringRef SqlEditorForm::do_connect(grt::GRT *grt, boost::shared_ptr<sql::TunnelConnection> tunnel, sql::Authentication::Ref &auth, ConnectionErrorInfo *err_ptr)
{
  try
  {
    RecMutexLock aux_dbc_conn_mutex(_aux_dbc_conn_mutex);
    RecMutexLock usr_dbc_conn_mutex(_usr_dbc_conn_mutex);

    _aux_dbc_conn->ref.reset();
    _usr_dbc_conn->ref.reset();

    // connection info
    _connection_details["name"] = _connection->name();
    _connection_details["hostName"] = _connection->parameterValues().get_string("hostName");
    _connection_details["port"] = strfmt("%li\n", _connection->parameterValues().get_int("port"));
    _connection_details["socket"] = _connection->parameterValues().get_string("socket");
    _connection_details["driverName"] = _connection->driver()->name();
    _connection_details["userName"] = _connection->parameterValues().get_string("userName");
      
    // Connection:
    _connection_info = std::string("<html><body style=\"font-family:") + DEFAULT_FONT_FAMILY + 
      "; font-size: 8pt\"><div style=\"color=#3b3b3b; font-weight:bold\">Connection:</div>";
    _connection_info.append(create_html_line("Name: ", _connection->name()));
    // Host:
    if (_connection->driver()->name() == "MysqlNativeSocket")
    {
#ifdef _WIN32
      std::string name = _connection->parameterValues().get_string("socket", "");
      if (name.empty())
        name = "pipe";
#else
      std::string name = _connection->parameterValues().get_string("socket", "");
      if (name.empty())
        name = "UNIX socket";
#endif
      _connection_info.append(create_html_line("Host:", "localhost (" + name + ")"));
    }
    else
    {
      _connection_info.append(create_html_line("Host:", _connection->parameterValues().get_string("hostName")));
      _connection_info.append(create_html_line("Port:", strfmt("%i", (int)_connection->parameterValues().get_int("port"))));
    }

    // open connections
    create_connection(_aux_dbc_conn, _connection, tunnel, auth, _aux_dbc_conn->autocommit_mode, false);
    create_connection(_usr_dbc_conn, _connection, tunnel, auth, _usr_dbc_conn->autocommit_mode, true);

    cache_sql_mode();

    try
    {
      {
        std::string value;
        get_session_variable(_usr_dbc_conn->ref.get(), "version_comment", value);
        _connection_details["dbmsProductName"] = value;
        get_session_variable(_usr_dbc_conn->ref.get(), "version", value);
        _connection_details["dbmsProductVersion"] = value;
      }
      
      _version = parse_version(grt, _connection_details["dbmsProductVersion"]);
      _version->name(grt::StringRef(_connection_details["dbmsProductName"]));

      db_query_EditorRef editor(_wbsql->get_grt_editor_object(this));
      if (editor.is_valid()) // this will be valid only on reconnections
        editor->serverVersion(_version);

      // Server:
      _connection_info.append(create_html_line("Server:", _connection_details["dbmsProductName"]));
      _connection_info.append(create_html_line("Version:",  _connection_details["dbmsProductVersion"]));
      // User:
      _connection_info.append(create_html_line("Login User:", _connection->parameterValues().get_string("userName")));
      
      // check the actual user we're logged in as
      if (_usr_dbc_conn && _usr_dbc_conn->ref.get())
      {
        boost::scoped_ptr<sql::Statement> statement(_usr_dbc_conn->ref->createStatement());
        boost::scoped_ptr<sql::ResultSet> rs(statement->executeQuery("SELECT current_user()"));
        if (rs->next())
          _connection_info.append(create_html_line("Current User:", rs->getString(1)));
      }

      // get lower_case_table_names value
      std::string value;
      if (_usr_dbc_conn && get_session_variable(_usr_dbc_conn->ref.get(), "lower_case_table_names", value))
        _lower_case_table_names = atoi(value.c_str());
    }
    CATCH_ANY_EXCEPTION_AND_DISPATCH(_("Get connection information"));
  }
  catch (sql::AuthenticationError &exc)
  {
    err_ptr->auth_error = new sql::AuthenticationError(exc);
    throw;
  }
  catch (sql::SQLException &exc)
  {
    log_exception("SqlEditorForm: exception in do_connect method", exc);

    if (exc.getErrorCode() == 1820) // ER_MUST_CHANGE_PASSWORD_LOGIN
      err_ptr->password_expired = true;
    else if (exc.getErrorCode() == 2013 || exc.getErrorCode() == 2003 || exc.getErrorCode() == 2002) // ERROR 2003 (HY000): Can't connect to MySQL server on X.Y.Z.W (or via socket)
    {
      _connection_info.append(create_html_line("", "<b><span style='color: red'>NO CONNECTION</span></b>"));
      add_log_message(WarningMsg, exc.what(), "Could not connect, server may not be running.", "");

      err_ptr->server_probably_down = true;

      // if there's no connection, then we continue anyway if this is a local connection or
      // a remote connection with remote admin enabled.. 
      grt::Module *m = _grtm->get_grt()->get_module("WbAdmin");
      grt::BaseListRef args(_grtm->get_grt());
      args.ginsert(_connection);
      if (!m || *grt::IntegerRef::cast_from(m->call_function("checkConnectionForRemoteAdmin", args)) == 0)
      {
        log_error("Connection failed but remote admin does not seem to be available, rethrowing exception...\n");
        throw;
      }
      log_info("Error %i connecting to server, assuming server is down and opening editor with no connection\n",
        exc.getErrorCode());
      _connection_info.append("</body></html>");
      return grt::StringRef();
    }
    _connection_info.append("</body></html>");
    throw;
  }
  
  _connection_info.append("</body></html>");
  return grt::StringRef();
}

//--------------------------------------------------------------------------------------------------

base::RecMutexLock SqlEditorForm::get_autocompletion_connection(sql::Dbc_connection_handler::Ref &conn)
{
  RecMutexLock lock(ensure_valid_aux_connection());
  conn = _aux_dbc_conn;
  return lock;
}

//--------------------------------------------------------------------------------------------------

/**
 * Triggered when the auto completion cache switches activity. We use this to update our busy
 * indicator.
 */
void SqlEditorForm::on_cache_action(bool active)
{
  _live_tree->mark_busy(active);
}

//--------------------------------------------------------------------------------------------------

bool SqlEditorForm::connected() const
{
  bool is_locked = false;
  {
    base::RecMutexTryLock tmp(_usr_dbc_conn_mutex);
    is_locked = !tmp.locked(); //is conn mutex is locked by someone else, then we assume the conn is in use and thus, there'a a connection.
  }
  if (_usr_dbc_conn && (is_locked || _usr_dbc_conn->ref.get_ptr()))
    return true; // we don't need to PING the server every time we want to check if the editor is connected
  return false;
}


bool SqlEditorForm::ping() const
{
  {
    base::RecMutexTryLock tmp(_usr_dbc_conn_mutex);
    if (!tmp.locked()) //is conn mutex is locked by someone else, then we assume the conn is in use and thus, there'a a connection.
      return true;

    if (_usr_dbc_conn && _usr_dbc_conn->ref.get_ptr())
    {
      std::auto_ptr<sql::Statement> stmt(_usr_dbc_conn->ref->createStatement());
      try
      {
        std::auto_ptr<sql::ResultSet> result(stmt->executeQuery("select 1"));
        return true;
      }
      catch (...)
      {
        // failed
      }
    }
  }
  return false;
}

base::RecMutexLock SqlEditorForm::ensure_valid_aux_connection(sql::Dbc_connection_handler::Ref &conn)
{
  RecMutexLock lock(ensure_valid_dbc_connection(_aux_dbc_conn, _aux_dbc_conn_mutex));
  conn = _aux_dbc_conn;
  return lock;
}


RecMutexLock SqlEditorForm::ensure_valid_aux_connection()
{
  return ensure_valid_dbc_connection(_aux_dbc_conn, _aux_dbc_conn_mutex);
}


RecMutexLock SqlEditorForm::ensure_valid_usr_connection()
{
  return ensure_valid_dbc_connection(_usr_dbc_conn, _usr_dbc_conn_mutex);
}


void SqlEditorForm::close_connection(sql::Dbc_connection_handler::Ref &dbc_conn)
{
  sql::Dbc_connection_handler::Ref myref(dbc_conn);
  if (dbc_conn && dbc_conn->ref.get_ptr())
  {
    try
    {
      dbc_conn->ref->close();
    }
    catch (sql::SQLException &)
    {
      // ignore if the connection is already closed
    }
  }
}


RecMutexLock SqlEditorForm::ensure_valid_dbc_connection(sql::Dbc_connection_handler::Ref &dbc_conn, base::RecMutex &dbc_conn_mutex)
{
  RecMutexLock mutex_lock(dbc_conn_mutex);
  bool valid= false;

  sql::Dbc_connection_handler::Ref myref(dbc_conn);
  if (dbc_conn && dbc_conn->ref.get_ptr())
  {
    if (dbc_conn->ref->isClosed())
    {
      bool user_connection = _usr_dbc_conn ? dbc_conn->ref.get_ptr() == _usr_dbc_conn->ref.get_ptr() : false;

      if (dbc_conn->autocommit_mode)
      {
        sql::AuthenticationSet authset;
        boost::shared_ptr<sql::TunnelConnection> tunnel = sql::DriverManager::getDriverManager()->getTunnel(_connection);
        
        create_connection(dbc_conn, _connection, tunnel, sql::Authentication::Ref(), dbc_conn->autocommit_mode, user_connection);
        if (!dbc_conn->ref->isClosed())
          valid= true;
      }
    }
    else
      valid= true;
  }
  if (!valid)
    throw grt::db_not_connected("DBMS connection is not available");

  return mutex_lock;
}


bool SqlEditorForm::auto_commit()
{
  if (_usr_dbc_conn)
    return _usr_dbc_conn->autocommit_mode;
  return false;
}


void SqlEditorForm::auto_commit(bool value)
{
  if (!_usr_dbc_conn)
    return;
  {
    const char *STATEMENT= value ? "AUTOCOMMIT=1" : "AUTOCOMMIT=0";
    try
    {
      RecMutexLock usr_dbc_conn_mutex = ensure_valid_usr_connection();
      _usr_dbc_conn->ref->setAutoCommit(value);
      _usr_dbc_conn->autocommit_mode= _usr_dbc_conn->ref->getAutoCommit();
    }
    CATCH_ANY_EXCEPTION_AND_DISPATCH(STATEMENT)
  }
  update_menu_and_toolbar();
}


void SqlEditorForm::toggle_autocommit()
{
  auto_commit(!auto_commit());
  update_menu_and_toolbar();
}


void SqlEditorForm::toggle_collect_field_info()
{
  if (_connection.is_valid())
    _connection->parameterValues().set("CollectFieldMetadata", grt::IntegerRef(collect_field_info() ? 0 : 1));
  update_menu_and_toolbar();
}

bool SqlEditorForm::collect_field_info() const
{
  if (_connection.is_valid())
    return _connection->parameterValues().get_int("CollectFieldMetadata", 1) != 0;
  return false;
}

void SqlEditorForm::toggle_collect_ps_statement_events()
{
  if (_connection.is_valid())
    _connection->parameterValues().set("CollectPerfSchemaStatsForQueries", grt::IntegerRef(collect_ps_statement_events() ? 0 : 1));
  update_menu_and_toolbar();
}


bool SqlEditorForm::collect_ps_statement_events() const
{
  if (_connection.is_valid() && is_supported_mysql_version_at_least(rdbms_version(), 5, 6))
    return _connection->parameterValues().get_int("CollectPerfSchemaStatsForQueries", 1) != 0;
  return false;
}

void SqlEditorForm::cancel_query()
{
  std::string query_kill_query;
  {
    db_mgmt_RdbmsRef rdbms= db_mgmt_RdbmsRef::cast_from(_connection->driver()->owner());
    SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms);
    Sql_specifics::Ref sql_specifics= sql_facade->sqlSpecifics();
    query_kill_query= sql_specifics->query_kill_query(_usr_dbc_conn->id);
  }
  if (query_kill_query.empty())
    return;

  const char *STATEMENT= "INTERRUPT";
  RowId log_message_index= add_log_message(DbSqlEditorLog::BusyMsg, _("Running..."), STATEMENT, "");
  Timer timer(false);

  try
  {
    {
      RecMutexLock aux_dbc_conn_mutex(ensure_valid_aux_connection());
      std::auto_ptr<sql::Statement> stmt(_aux_dbc_conn->ref->createStatement());
      {
        ScopeExitTrigger schedule_timer_stop(boost::bind(&Timer::stop, &timer));
        timer.run();
        stmt->execute(query_kill_query);
        
        // this can potentially cause threading issues, since connector driver isn't thread-safe
        //close_connection(_usr_dbc_conn);

        // connection drop doesn't interrupt fetching stage (surprisingly)
        // to workaround that we set special flag and check it periodically during fetching
        _usr_dbc_conn->is_stop_query_requested= is_running_query();
      }
    }

    if (_usr_dbc_conn->is_stop_query_requested)
    {
      _grtm->replace_status_text("Query Cancelled");
      set_log_message(log_message_index, DbSqlEditorLog::NoteMsg, _("OK - Query cancelled"), STATEMENT, timer.duration_formatted());
    }
    else
      set_log_message(log_message_index, DbSqlEditorLog::NoteMsg, _("OK - Query already completed"), STATEMENT, timer.duration_formatted());

    // reconnect but only if in autocommit mode
    if (_usr_dbc_conn->autocommit_mode)
    {
      // this will restore connection if it was established previously
      exec_sql_task->execute_in_main_thread(
        boost::bind(&SqlEditorForm::send_message_keep_alive, this),
        false,
        true);
    }
  }
  CATCH_SQL_EXCEPTION_AND_DISPATCH(STATEMENT, log_message_index, "")
}


void SqlEditorForm::commit()
{
  exec_sql_retaining_editor_contents("COMMIT",MySQLEditor::Ref(), false);
}


void SqlEditorForm::rollback()
{
  exec_sql_retaining_editor_contents("ROLLBACK",MySQLEditor::Ref(), false);
}


void SqlEditorForm::explain_sql()
{
  size_t start, end;
 MySQLEditor::Ref sql_editor_= active_sql_editor();
  if (sql_editor_)
  {
    sql_editor_->selected_range(start, end);
    std::string sql= sql_editor_->sql();
    if (start != end)
      sql= sql.substr(start, end-start);

    do_explain_sql(sql);
  }
}


void SqlEditorForm::explain_current_statement()
{
 MySQLEditor::Ref sql_editor_= active_sql_editor();
  if (sql_editor_)
    do_explain_sql(sql_editor_->current_statement());
}


void SqlEditorForm::do_explain_sql(const std::string &sql)
{
  SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms());
  std::list<std::string> statements;
  sql_facade->splitSqlScript(sql, statements);
  Sql_syntax_check::Ref sql_syntax_check= sql_facade->sqlSyntaxCheck();
  std::string sql_script;
  for (std::list<std::string>::iterator i= statements.begin(), i_end= statements.end(); i != i_end; ++i)
    if (Sql_syntax_check::sql_select == sql_syntax_check->determine_statement_type(*i))
      sql_script+= "EXPLAIN " + *i + ";\n";

  exec_sql_retaining_editor_contents(sql_script, active_sql_editor(), false);
}


void SqlEditorForm::exec_sql_retaining_editor_contents(const std::string &sql_script,MySQLEditor::Ref editor, bool sync, bool dont_add_limit_clause)
{
  auto_save();

  if (!connected())
    throw grt::db_not_connected("Not connected");
  
  RecordsetsRef recordsets;
  int i = sql_editor_index(editor);
  if (i >= 0)
    recordsets = sql_editor_recordsets(i);
  else
    recordsets = RecordsetsRef(new Recordsets());  
  
  exec_sql_task->exec(sync,
    boost::bind(&SqlEditorForm::do_exec_sql, this, _1,
               weak_ptr_from(this), boost::shared_ptr<std::string>(new std::string(sql_script)),
               editor,(ExecFlags)(Retaining | (dont_add_limit_clause?DontAddLimitClause:0)),
               recordsets));
}

void SqlEditorForm::run_editor_contents(bool current_statement_only)
{
 MySQLEditor::Ref editor(active_sql_editor());
  if (editor)
  {
    if (exec_editor_sql(editor, false, current_statement_only, current_statement_only))
    {
      // Show busy animation only if the execution actually started.
      do_partial_ui_refresh(SqlEditorForm::QueryExecutionStarted);
    }
  }
}

RecordsetsRef SqlEditorForm::exec_sql_returning_results(const std::string &sql_script, bool dont_add_limit_clause)
{
  if (!connected())
    throw grt::db_not_connected("Not connected");

  RecordsetsRef rsets(new Recordsets());
  
  do_exec_sql(_grtm->get_grt(), weak_ptr_from(this), boost::shared_ptr<std::string>(new std::string(sql_script)),
   MySQLEditor::Ref(), (ExecFlags)(dont_add_limit_clause?DontAddLimitClause:0), rsets);
  
  return rsets;
}

/**
 * Runs the current content of the given editor on the target server and returns true if the query
 * was actually started (useful for the platform layers to show a busy animation).
 * 
 * @param editor The editor whose content is to be executed.
 * @param sync If true wait for completion.
 * @param current_statement_only If true then only the statement where the cursor is in is executed.
 *                               Otherwise the current selection is executed (if there is one) or
 *                               the entire editor content.
 * @param use_non_std_delimiter If true the code is wrapped with a non standard delimiter to
 *                                    allow running the sql regardless of the delimiters used by the
 *                                    user (e.g. for view/sp definitions).
 * @param dont_add_limit_clause If true the automatic addition of the LIMIT clause is suppressed, which
 *                              is used to limit on the number of return rows (avoid huge result sets
 *                              by accident).
 */

bool SqlEditorForm::exec_editor_sql(MySQLEditor::Ref editor, bool sync, bool current_statement_only, 
  bool use_non_std_delimiter, bool dont_add_limit_clause)
{
  editor->cancel_auto_completion();

  boost::shared_ptr<std::string> shared_sql;
  if (current_statement_only)
    shared_sql.reset(new std::string(editor->current_statement()));
  else
  {
    std::string sql = editor->selected_text();
    if (sql.empty())
    {
      std::pair<const char*, size_t> text = editor->text_ptr();
      shared_sql.reset(new std::string(text.first, text.second));
    }
    else
      shared_sql.reset(new std::string(sql));
  }

  if (shared_sql->empty())
    return false;

  ExecFlags flags = (ExecFlags)0;
  
  if (use_non_std_delimiter)
    flags = (ExecFlags)(flags | NeedNonStdDelimiter);
  if (dont_add_limit_clause)
    flags = (ExecFlags)(flags | DontAddLimitClause);
  if (_grtm->get_app_option_int("DbSqlEditor:ShowWarnings", 1))
    flags = (ExecFlags)(flags | ShowWarnings);
  auto_save();
  
  RecordsetsRef recordsets;

  int i = sql_editor_index(editor);
  if (i >= 0)
    recordsets = sql_editor_recordsets(i);
  else
    recordsets = RecordsetsRef(new Recordsets());

  exec_sql_task->exec(
    sync,
    boost::bind(&SqlEditorForm::do_exec_sql, this, _1, weak_ptr_from(this), shared_sql,
    editor, flags, recordsets)
    );

  return true;
}

struct GuardBoolFlag
{
  bool *flag;
  GuardBoolFlag(bool *ptr) : flag(ptr) { if (flag) *flag = true; }
  ~GuardBoolFlag() { if (flag) *flag = false; }
};
void SqlEditorForm::update_live_schema_tree(const std::string &sql)
{
  if(_grtm)
    _grtm->run_once_when_idle(this, boost::bind(&SqlEditorForm::handle_command_side_effects, this, sql));
}

grt::StringRef SqlEditorForm::do_exec_sql(grt::GRT *grt, Ptr self_ptr, boost::shared_ptr<std::string> sql,
 MySQLEditor::Ref editor, ExecFlags flags, RecordsetsRef result_list)
{
  bool retaining = (flags & Retaining) != 0;
  bool use_non_std_delimiter = (flags & NeedNonStdDelimiter) != 0;
  bool dont_add_limit_clause = (flags & DontAddLimitClause) != 0;
  std::map<std::string, boost::int64_t> ps_stats;
  bool fetch_field_info = collect_field_info();
  bool query_ps_stats = collect_ps_statement_events();
  std::string query_ps_statement_events_error;
  std::string statement;
  int max_query_size_to_log = _grtm->get_app_option_int("DbSqlEditor:MaxQuerySizeToHistory", 0);
  int limit_rows = 0;
  if (_grtm->get_app_option_int("SqlEditor:LimitRows") != 0)
    limit_rows = _grtm->get_app_option_int("SqlEditor:LimitRowsCount", 0);

  _grtm->replace_status_text(_("Executing Query..."));

  RETVAL_IF_FAIL_TO_RETAIN_WEAK_PTR (SqlEditorForm, self_ptr, self, grt::StringRef(""))

  // add_log_message() will increment this variable on errors or warnings
  _exec_sql_error_count = 0;

  bool interrupted = true;
  Mutex *result_list_mutex = NULL;
  sql::Driver *dbc_driver= NULL;
  int editor_index = -1;
  try
  {
    int default_seq = 0;
    int *rs_sequence = &default_seq;
    bool *busy_flag = 0;

    if (editor && !retaining)
    {
     EditorInfo::Ref info;
      editor_index = 0;
      for (Sql_editors::iterator info_ = _sql_editors.begin(); info_ != _sql_editors.end(); ++info_, ++editor_index)
      {
        if ((*info_)->editor == editor)
        {
          info = *info_;
          rs_sequence = &info->rs_sequence;
          busy_flag = &info->busy;
          break;
        }
      }
      
      GuardBoolFlag guard_busy_flag(busy_flag);

      // close all recordsets for the same editor
      if (info)
      {
        MutexTryLock recordsets_mutex(info->recordset_mutex);
        if (!recordsets_mutex.locked())
          throw std::runtime_error("The editor is busy and cannot execute the query now. Please try again later.");
            
        RecordsetsRef recordsets(new Recordsets());
        recordsets->reserve(result_list->size());
        ssize_t index = result_list->size();
        while (--index >= 0)
        {
          if (!(*result_list)[index]->can_close(false))
            recordsets->push_back((*result_list)[index]);
          else
          {
            // This must perform the same as on_close_recordset(), but without the redundant mutex locking.
            RecordsetsRef rsets = info->recordsets;
            Recordset::Ref current_recordset = (*result_list)[index];
            Recordsets::iterator iter = std::find(rsets->begin(), rsets->end(), current_recordset);
            if (iter != rsets->end())
            {
              if (info->active_result && info->active_result->recordset() == *iter)
                info->active_result.reset();
              rsets->erase(iter);
            }
            if (editor_index >= 0)
              recordset_list_changed(editor_index, current_recordset, false);
          }
        }
        result_list->swap(*recordsets.get());
        result_list_mutex = &info->recordset_mutex;
      }
    }

    RecMutexLock use_dbc_conn_mutex(ensure_valid_usr_connection());

    GuardBoolFlag guard_busy_flag(busy_flag);
    
    dbc_driver= _usr_dbc_conn->ref->getDriver();
    dbc_driver->threadInit();

    bool is_running_query= true;
    AutoSwap<bool> is_running_query_keeper(_is_running_query, is_running_query);
    update_menu_and_toolbar();

    _has_pending_log_messages= false;
    ScopeExitTrigger schedule_log_messages_refresh(boost::bind(
      &SqlEditorForm::refresh_log_messages, this, true));

    SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms());
    Sql_syntax_check::Ref sql_syntax_check= sql_facade->sqlSyntaxCheck();
    Sql_specifics::Ref sql_specifics= sql_facade->sqlSpecifics();

    bool ran_set_sql_mode = false;
    bool logging_queries;
    std::vector<std::pair<size_t, size_t> > statement_ranges;
    sql_facade->splitSqlScript(sql->c_str(), sql->size(),
      use_non_std_delimiter ? sql_specifics->non_std_sql_delimiter() : ";", statement_ranges);

    if (statement_ranges.size() > 1)
    {
      query_ps_stats = false;
      query_ps_statement_events_error = "Query stats can only be fetched when a single statement is executed.";
    }

    if (!max_query_size_to_log || max_query_size_to_log >= (int)sql->size())
    {
      logging_queries = true;
    }
    else
    {
      std::list<std::string> warning;
      warning.push_back(base::strfmt("Skipping history entries for %li statements, total %li bytes", statement_ranges.size(),
                                     sql->size()));
      _history->add_entry(warning);
      logging_queries = false;
    }

    std::pair<size_t, size_t> statement_range;
    BOOST_FOREACH (statement_range, statement_ranges)
    {
      statement = sql->substr(statement_range.first, statement_range.second);
      std::list<std::string> sub_statements;
      sql_facade->splitSqlScript(statement, sub_statements);
      size_t multiple_statement_count = sub_statements.size();
      bool is_multiple_statement = (1 < multiple_statement_count);

      {
        statement= strip_text(statement, false, true);
        if (statement.empty())
          continue;

        Sql_syntax_check::Statement_type statement_type= sql_syntax_check->determine_statement_type(statement);
        if (Sql_syntax_check::sql_empty == statement_type)
          continue;

        std::string schema_name;
        std::string table_name;

        if (logging_queries)
        {
          std::list<std::string> statements;
          statements.push_back(statement);
          _history->add_entry(statements);
        }

        Recordset_cdbc_storage::Ref data_storage;

        // for select queries add limit clause if specified by global option
        if (!is_multiple_statement && (Sql_syntax_check::sql_select == statement_type))
        {
          data_storage= Recordset_cdbc_storage::create(_grtm);
          data_storage->set_gather_field_info(fetch_field_info);
          data_storage->rdbms(rdbms());
          data_storage->dbms_conn(_usr_dbc_conn);
          data_storage->aux_dbms_conn(_aux_dbc_conn);
          
          SqlFacade::String_tuple_list column_names;

          if (!table_name.empty() || sql_facade->parseSelectStatementForEdit(statement, schema_name, table_name, column_names))
          {
            data_storage->schema_name(schema_name.empty() ? _usr_dbc_conn->active_schema : schema_name);
            data_storage->table_name(table_name);
          }
          else
            data_storage->readonly_reason("Statement must be a SELECT for columns of a single table with a primary key for its results to be editable.");

          data_storage->sql_query(statement);

          {
            bool do_limit = !dont_add_limit_clause && limit_rows > 0;
            data_storage->limit_rows(do_limit);

            if (limit_rows > 0)
              data_storage->limit_rows_count(limit_rows);
          }
          statement= data_storage->decorated_sql_query();
        }

        {
          RowId log_message_index= add_log_message(DbSqlEditorLog::BusyMsg, _("Running..."), statement,
            ((Sql_syntax_check::sql_select == statement_type) ? "? / ?" : "?"));

          bool statement_failed= false;
          long long updated_rows_count= -1;
          Timer statement_exec_timer(false);
          Timer statement_fetch_timer(false);
          boost::shared_ptr<sql::Statement> dbc_statement(_usr_dbc_conn->ref->createStatement());
          bool is_result_set_first= false;

          if (_usr_dbc_conn->is_stop_query_requested)
            throw std::runtime_error(_("Query execution has been stopped, the connection to the DB server was not restarted, any open transaction remains open"));

          try
          {
            {
              ScopeExitTrigger schedule_statement_exec_timer_stop(boost::bind(&Timer::stop, &statement_exec_timer));
              statement_exec_timer.run();
              is_result_set_first= dbc_statement->execute(statement);
            }
            updated_rows_count= dbc_statement->getUpdateCount();
            if (Sql_syntax_check::sql_use == statement_type)
              cache_active_schema_name();
            if (Sql_syntax_check::sql_set == statement_type && statement.find("@sql_mode") != std::string::npos)
              ran_set_sql_mode= true;
            if (Sql_syntax_check::sql_drop == statement_type)
              update_live_schema_tree(statement);
          }
          catch (sql::SQLException &e)
          {
            std::string err_msg;
            // safe mode
            switch (e.getErrorCode())
            {
              case 1046: // not default DB selected
                err_msg= strfmt(_("Error Code: %i. %s\nSelect the default DB to be used by double-clicking its name in the SCHEMAS list in the sidebar."), e.getErrorCode(), e.what());
                break;
              case 1175: // safe mode
                err_msg= strfmt(_("Error Code: %i. %s\nTo disable safe mode, toggle the option in Preferences -> SQL Queries and reconnect."), e.getErrorCode(), e.what());
                break;
              default:
                err_msg= strfmt(_("Error Code: %i. %s"), e.getErrorCode(), e.what());
                break;
            }
            set_log_message(log_message_index, DbSqlEditorLog::ErrorMsg, err_msg, statement, statement_exec_timer.duration_formatted());
            statement_failed= true;
          }
          catch (std::exception &e)
          {
            std::string err_msg= strfmt(_("Error: %s"), e.what());
            set_log_message(log_message_index, DbSqlEditorLog::ErrorMsg, err_msg, statement, statement_exec_timer.duration_formatted());
            statement_failed= true;
          }
          if (statement_failed)
          {
            if (_continue_on_error)
              continue; // goto next statement
            else
              goto stop_processing_sql_script;
          }

          sql::mysql::MySQL_Connection* mysql_connection = dynamic_cast<sql::mysql::MySQL_Connection*>(dbc_statement->getConnection());
          sql::SQLString last_statement_info;
          if (mysql_connection != NULL)
            last_statement_info = mysql_connection->getLastStatementInfo();
          if (updated_rows_count >= 0)
          {
            std::string message = strfmt(_("%lli row(s) affected"), updated_rows_count);
            bool has_warning = false;
            if (flags & ShowWarnings)
            {
              std::string warnings_message;
              const sql::SQLWarning *warnings= dbc_statement->getWarnings();
              if (warnings)
              {
                int count= 0;
                const sql::SQLWarning *w = warnings;
                while (w) 
                {
                  warnings_message.append(strfmt("\n%i %s", w->getErrorCode(), w->getMessage().c_str()));
                  count++; 
                  w= w->getNextWarning();
                }
                message.append(strfmt(_(", %i warning(s):"), count));
                has_warning = true;
              }
              if (!warnings_message.empty())
                message.append(warnings_message);
            }
            if (!last_statement_info->empty())
              message.append("\n").append(last_statement_info);
            set_log_message(log_message_index, has_warning ? DbSqlEditorLog::WarningMsg : DbSqlEditorLog::OKMsg, message, statement, statement_exec_timer.duration_formatted());            
          }

          if (query_ps_stats)
            query_ps_statistics(_usr_dbc_conn->id, ps_stats);

          int resultset_count= 0;
          bool more_results= is_result_set_first;
          bool reuse_log_msg= false;
          if ((updated_rows_count < 0) || is_multiple_statement)
          {
            for (size_t processed_substatements_count= 0; processed_substatements_count < multiple_statement_count; ++processed_substatements_count)
            {
              do
              {
                if (more_results)
                {
                  if (!reuse_log_msg && ((updated_rows_count >= 0) || (resultset_count)))
                    log_message_index= add_log_message(DbSqlEditorLog::BusyMsg, _("Fetching..."), statement, "- / ?");
                  else
                    set_log_message(log_message_index, DbSqlEditorLog::BusyMsg, _("Fetching..."), statement, statement_exec_timer.duration_formatted() + " / ?");
                  reuse_log_msg= false;
                  boost::shared_ptr<sql::ResultSet> dbc_resultset;
                  {
                    ScopeExitTrigger schedule_statement_fetch_timer_stop(boost::bind(&Timer::stop, &statement_fetch_timer));
                    statement_fetch_timer.run();
                    
                    // need a separate exception catcher here, because sometimes a query error
                    // will only throw an exception after fetching starts, which causes the busy spinner
                    // to be active forever, since the exception is logged in a new log_id/row
                    // XXX this could also be caused by a bug in Connector/C++
                    try
                    {
                      dbc_resultset.reset(dbc_statement->getResultSet());
                    }
                    catch (sql::SQLException &e)
                    {
                      std::string err_msg;
                      // safe mode
                      switch (e.getErrorCode())
                      {
                        case 1046: // not default DB selected
                        err_msg= strfmt(_("Error Code: %i. %s\nSelect the default DB to be used by double-clicking its name in the SCHEMAS list in the sidebar."), e.getErrorCode(), e.what());
                        break;
                        case 1175: // safe mode
                        err_msg= strfmt(_("Error Code: %i. %s\nTo disable safe mode, toggle the option in Preferences -> SQL Queries and reconnect."), e.getErrorCode(), e.what());
                        break;
                        default:
                        err_msg= strfmt(_("Error Code: %i. %s"), e.getErrorCode(), e.what());
                        break;
                      }
                      set_log_message(log_message_index, DbSqlEditorLog::ErrorMsg, err_msg, statement, statement_exec_timer.duration_formatted());
                      
                      if (_continue_on_error)
                        continue; // goto next statement
                      else
                        goto stop_processing_sql_script;
                    }
                  }
                  if (dbc_resultset)
                  {
                    if (!data_storage)
                    {
                      data_storage= Recordset_cdbc_storage::create(_grtm);
                      data_storage->set_gather_field_info(fetch_field_info);
                      data_storage->rdbms(rdbms());
                      data_storage->dbms_conn(_usr_dbc_conn);
                      data_storage->aux_dbms_conn(_aux_dbc_conn);
                      if (table_name.empty())
                        data_storage->sql_query(statement);
                      data_storage->schema_name(schema_name);
                      data_storage->table_name(table_name);
                    }

                    data_storage->dbc_statement(dbc_statement);
                    data_storage->dbc_resultset(dbc_resultset);
                    data_storage->reloadable(!is_multiple_statement && (Sql_syntax_check::sql_select == statement_type));

                    Recordset::Ref rs= Recordset::create(exec_sql_task);
                    rs->is_field_value_truncation_enabled(true);
                    rs->apply_changes_cb= boost::bind(&SqlEditorForm::apply_changes_to_recordset, this, Recordset::Ptr(rs));
                    rs->on_close.connect(boost::bind(&SqlEditorForm::on_close_recordset, this, _1));
                    rs->caption(strfmt("%s %i",
                                       (table_name.empty() ? _("Result") : table_name.c_str()),
                                       ++*rs_sequence));
                    rs->generator_query(statement);

                    {
                      RecordsetData *rdata = new RecordsetData();
                      rdata->duration = statement_exec_timer.duration();
		      rdata->editor =MySQLEditor::Ptr(editor); 
                      rdata->ps_stat_error = query_ps_statement_events_error;
                      rdata->ps_stat_info = ps_stats;
                      rs->set_client_data(rdata);
                    }

                    scoped_connect(rs->get_context_menu()->signal_will_show(),
                          boost::bind(&SqlEditorForm::on_recordset_context_menu_show, this, Recordset::Ptr(rs),MySQLEditor::Ptr(editor)));
                    rs->action_list().register_action("recall_query",
                      boost::bind(&SqlEditorForm::recall_recordset_query, this, Recordset::Ptr(rs)));

                    // XXX: refresh recordset title on status bar change? Huh?
                    scoped_connect(&rs->refresh_ui_status_bar_signal,
                      boost::bind(&bec::RefreshUI::do_partial_ui_refresh, this, (int)RefreshRecordsetTitle));

                    rs->data_storage(data_storage);

                    {
                      //We need this mutex, because reset(bool) is using aux_connection
                      //to query bestrowidentifier.
                      RecMutexLock aux_mtx(ensure_valid_aux_connection(_aux_dbc_conn));
                      rs->reset(true);
                    }

                    if (data_storage->valid()) // query statement
                    {
                      if (result_list_mutex)
                      {
                        MutexLock recordsets_mutex(*result_list_mutex);
                        result_list->push_back(rs);
                      }
                      else
                        result_list->push_back(rs);

                      int editor_index = sql_editor_index(editor);
                      if (editor_index >= 0)
                        recordset_list_changed(editor_index, rs, true);
                      std::string statement_res_msg = base::to_string(rs->row_count()) + _(" row(s) returned");
                      if (!last_statement_info->empty())
                        statement_res_msg.append("\n").append(last_statement_info);
                      std::string exec_and_fetch_durations=
                        (((updated_rows_count >= 0) || (resultset_count)) ? std::string("-") : statement_exec_timer.duration_formatted()) + " / " +
                        statement_fetch_timer.duration_formatted();
                      set_log_message(log_message_index, DbSqlEditorLog::OKMsg, statement_res_msg, statement, exec_and_fetch_durations);
                    }
                    //! else failed to fetch data
                    //added_recordsets.push_back(rs);
                    ++resultset_count;
                  }
                  else
                  {
                    reuse_log_msg= true;
                  }
                  data_storage.reset();
                }
              }
              while ((more_results= dbc_statement->getMoreResults()));
            }
          }
          
          if ((updated_rows_count < 0) && !(resultset_count))
          {
            set_log_message(log_message_index, DbSqlEditorLog::OKMsg, _("OK"), statement, statement_exec_timer.duration_formatted());
          }
        }
      }
    } // BOOST_FOREACH (statement, statements)

    _grtm->replace_status_text(_("Query Completed"));
    interrupted = false;

stop_processing_sql_script:
    if (interrupted)
      _grtm->replace_status_text(_("Query interrupted"));
    // try to minimize the times this is called, since this will change the state of the connection
    // after a user query is ran (eg, it will reset all warnings)
    if (ran_set_sql_mode)
      cache_sql_mode();
  }
  CATCH_ANY_EXCEPTION_AND_DISPATCH(statement)

  if (dbc_driver)
    dbc_driver->threadEnd();

  update_menu_and_toolbar();
  
  _usr_dbc_conn->is_stop_query_requested = false;

  return grt::StringRef("");
}


void SqlEditorForm::exec_management_sql(const std::string &sql, bool log)
{
  sql::Dbc_connection_handler::Ref conn;
  base::RecMutexLock lock(ensure_valid_aux_connection(conn));
  if (conn)
  {
    RowId rid = log ? add_log_message(DbSqlEditorLog::BusyMsg, _("Executing "), sql, "- / ?") : 0;
    boost::scoped_ptr<sql::Statement> stmt(conn->ref->createStatement());
    Timer statement_exec_timer(false);
    try
    {
      stmt->execute(sql);
    }
    catch (sql::SQLException &e)
    {
      if (log)
        set_log_message(rid, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), sql, "");
      throw;
    }
    CATCH_EXCEPTION_AND_DISPATCH(sql);

    if (log)
      set_log_message(rid, DbSqlEditorLog::OKMsg, _("OK"), sql, statement_exec_timer.duration_formatted());

    handle_command_side_effects(sql);
  }
}


void SqlEditorForm::exec_main_sql(const std::string &sql, bool log)
{
  base::RecMutexLock lock(ensure_valid_usr_connection());
  if (_usr_dbc_conn)
  {
    RowId rid = log ? add_log_message(DbSqlEditorLog::BusyMsg, _("Executing "), sql, "- / ?") : 0;
    boost::scoped_ptr<sql::Statement> stmt(_usr_dbc_conn->ref->createStatement());
    Timer statement_exec_timer(false);
    try
    {
      stmt->execute(sql);
    }
    catch (sql::SQLException &e)
    {
      if (log)
      set_log_message(rid, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), sql, "");
      throw;
    }
    CATCH_EXCEPTION_AND_DISPATCH(sql);
    
    if (log)
    set_log_message(rid, DbSqlEditorLog::OKMsg, _("OK"), sql, statement_exec_timer.duration_formatted());
    
    handle_command_side_effects(sql);
  }
}

static wb::LiveSchemaTree::ObjectType str_to_object_type(const std::string &object_type)
{
  if (object_type == "db.Table")
    return LiveSchemaTree::Table;
  else if (object_type == "db.View")
    return LiveSchemaTree::View;
  else if (object_type == "db.StoredProcedure")
    return LiveSchemaTree::Procedure;
  else if (object_type == "db.Function")
    return LiveSchemaTree::Function;
  else if (object_type == "db.Index")
    return LiveSchemaTree::Index;
  else if (object_type == "db.Trigger")
    return LiveSchemaTree::Trigger;
  else if (object_type == "db.Schema")
    return LiveSchemaTree::Schema;

  return LiveSchemaTree::None;
}

void SqlEditorForm::handle_command_side_effects(const std::string &sql)
{
  SqlFacade::Ref sql_facade= SqlFacade::instance_for_rdbms(rdbms());

  std::string object_type;
  std::string schema_name = active_schema();
  std::vector<std::pair<std::string, std::string> > object_names;

  // special hack, check for some special commands and update UI accordingly
  if (sql_facade->parseDropStatement(sql, object_type, object_names) && !object_names.empty())
  {

    wb::LiveSchemaTree::ObjectType obj = str_to_object_type(object_type);
    if (obj != wb::LiveSchemaTree::None)
    {
      std::vector<std::pair<std::string, std::string> >::reverse_iterator rit;

      if (obj == wb::LiveSchemaTree::Schema)
      {
        for (rit = object_names.rbegin(); rit != object_names.rend(); ++rit)
          _live_tree->refresh_live_object_in_overview(obj, (*rit).first, (*rit).first, "");

        if (!object_names.empty())
          schema_name = object_names.back().first;

        if ((schema_name.size() > 0) && (active_schema() == schema_name) && connection_descriptor().is_valid())
        {
          std::string default_schema= connection_descriptor()->parameterValues().get_string("schema", "");
          if (schema_name == default_schema)
            default_schema = "";
          _grtm->run_once_when_idle(this, boost::bind(&set_active_schema, shared_from_this(), default_schema));
        }
      }
      else
      {
        for (rit = object_names.rbegin(); rit != object_names.rend(); ++rit)
          _live_tree->refresh_live_object_in_overview(obj, (*rit).first.empty() ? schema_name : (*rit).first, (*rit).second, "");
      }
    }
  }
}

db_query_ResultsetRef SqlEditorForm::exec_management_query(const std::string &sql, bool log)
{
  sql::Dbc_connection_handler::Ref conn;
  base::RecMutexLock lock(ensure_valid_aux_connection(conn));
  if (conn)
  {
    RowId rid = log ? add_log_message(DbSqlEditorLog::BusyMsg, _("Executing "), sql, "- / ?") : 0;
    boost::scoped_ptr<sql::Statement> stmt(conn->ref->createStatement());
    Timer statement_exec_timer(false);
    try
    {
      boost::shared_ptr<sql::ResultSet> results(stmt->executeQuery(sql));

      if (log)
        set_log_message(rid, DbSqlEditorLog::OKMsg, _("OK"), sql, statement_exec_timer.duration_formatted());

      return grtwrap_recordset(wbsql()->get_grt_editor_object(this), results);
    }
    catch (sql::SQLException &e)
    {
      if (log)
        set_log_message(rid, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), sql, "");
      throw;
    }
  }
  return db_query_ResultsetRef();
}


db_query_ResultsetRef SqlEditorForm::exec_main_query(const std::string &sql, bool log)
{
  base::RecMutexLock lock(ensure_valid_usr_connection());
  if (_usr_dbc_conn)
  {
    RowId rid = log ? add_log_message(DbSqlEditorLog::BusyMsg, _("Executing "), sql, "- / ?") : 0;
    boost::scoped_ptr<sql::Statement> stmt(_usr_dbc_conn->ref->createStatement());
    Timer statement_exec_timer(false);
    try
    {
      boost::shared_ptr<sql::ResultSet> results(stmt->executeQuery(sql));
      
      if (log)
      set_log_message(rid, DbSqlEditorLog::OKMsg, _("OK"), sql, statement_exec_timer.duration_formatted());
      
      return grtwrap_recordset(wbsql()->get_grt_editor_object(this), results);
    }
    catch (sql::SQLException &e)
    {
      if (log)
        set_log_message(rid, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), sql, "");
      throw;
    }
  }
  return db_query_ResultsetRef();
}


bool SqlEditorForm::is_running_query()
{
  return _is_running_query;
}


void SqlEditorForm::continue_on_error(bool val)
{
  if (_continue_on_error == val)
    return;

  _continue_on_error= val;
  _grtm->set_app_option("DbSqlEditor:ContinueOnError", grt::IntegerRef((int)_continue_on_error));
  
  if (_menu)
    _menu->set_item_checked("query.stopOnError", !continue_on_error());
  set_editor_tool_items_checked("query.stopOnError", !continue_on_error());
}


void SqlEditorForm::send_message_keep_alive()
{
  try
  {
    // ping server and reset connection timeout counter
    // this also checks the connection state and restores it if possible
    ensure_valid_aux_connection();
    ensure_valid_usr_connection();
  }
  catch (const std::exception &)
  {
  }
}


void SqlEditorForm::active_result_panel(int editor, SqlEditorResult::Ref value)
{
  MutexLock lock(_sql_editors_mutex);
  if (editor >= 0 && editor < (int)_sql_editors.size())
  {
    bool found = false;
    _sql_editors[editor]->active_result = value;

    db_query_QueryEditorRef qeditor(db_query_QueryEditorRef::cast_from(_sql_editors[editor]->editor->grtobj()));
    for (size_t c = qeditor->resultsets().count(), i = 0; i < c; i++)
    {
      if (value && dynamic_cast<WBRecordsetResultset*>(qeditor->resultsets()[i]->get_data())->recordset == value->recordset())
      {
        found = true;
        qeditor->activeResultset(qeditor->resultsets()[i]);
        break;
      }
    }
    if (!found)
      qeditor->activeResultset(db_query_ResultsetRef());
  }
  if (_menu)
  {
    Recordset::Ref rset(value ? value->recordset(): Recordset::Ref());

    _menu->set_item_enabled("query.save_edits", rset && rset->has_pending_changes());
    _menu->set_item_enabled("query.discard_edits", rset && rset->has_pending_changes());
    _menu->set_item_enabled("query.export", (bool)rset);
  }
}


SqlEditorResult::Ref SqlEditorForm::active_result_panel(int editor)
{
  MutexLock lock(_sql_editors_mutex);
  if (editor >= 0 && editor < (int)_sql_editors.size())
    return _sql_editors[editor]->active_result;
  return SqlEditorResult::Ref();
}


Recordset::Ref SqlEditorForm::active_recordset(int editor)
{
  SqlEditorResult::Ref result(active_result_panel(editor));
  if (result)
    result->recordset();
  return Recordset::Ref();
}


void SqlEditorForm::active_recordset(int editor, Recordset::Ref rset)
{
  if (rset)
  {
    RecordsetData *rdata = dynamic_cast<RecordsetData*>(rset->client_data());
    active_result_panel(editor, rdata->result_panel);
  }
  else
    active_result_panel(editor, SqlEditorResult::Ref());
}


bool SqlEditorForm::recordset_reorder(int editor, Recordset::Ref value, int new_index)
{
  MutexTryLock lock(_sql_editors_mutex);
  if (lock.locked())
  {
    if (editor >= 0 && editor < (int)_sql_editors.size())
    {
      MutexTryLock rlock(_sql_editors[editor]->recordset_mutex);
      if (rlock.locked())
      {
        RecordsetsRef recordsets(sql_editor_recordsets(editor));

        Recordsets::iterator iter = std::find(recordsets->begin(), recordsets->end(), value);
        if (iter != recordsets->end())
        {
          if (new_index >= (int)recordsets->size()-1)
            new_index= -1;
          recordsets->erase(iter);
          if (new_index < 0)
            recordsets->push_back(value);
          else
            recordsets->insert(recordsets->begin() + new_index, value);
          return true;
        }
      }
    }
  }
  return false;
}


void SqlEditorForm::on_close_recordset(Recordset::Ptr rs_ptr)
{
  int editor = 0;
  bool clear_recordset = false;
  RETURN_IF_FAIL_TO_RETAIN_WEAK_PTR (Recordset, rs_ptr, rs)
  {
    {
      MutexLock ed_lock(_sql_editors_mutex);
      editor = sql_editor_index_for_recordset(rs_ref->key());
      if (editor >= 0)
      {
        MutexLock recordsets_mutex(_sql_editors[editor]->recordset_mutex);
        RecordsetsRef rsets(sql_editor_recordsets(editor));
        rsets->erase(std::find(rsets->begin(), rsets->end(), rs_ref));
        if (_sql_editors[editor]->active_result && _sql_editors[editor]->active_result->recordset() == rs_ref)
          clear_recordset = true;
      }
    }
    if (clear_recordset)
      active_result_panel(editor, SqlEditorResult::Ref());

    recordset_list_changed(editor, rs_ref, false);
    //delme close_recordset_ui.emit(rs->key());    
  }
}

void SqlEditorForm::recall_recordset_query(Recordset::Ptr rs_ptr)
{
  RETURN_IF_FAIL_TO_RETAIN_WEAK_PTR (Recordset, rs_ptr, rs)
  {
    std::string query = rs->generator_query();
    
    new_sql_scratch_area();
    set_sql_editor_text(query.c_str());
  }
}

void SqlEditorForm::apply_changes_to_recordset(Recordset::Ptr rs_ptr)
{
  RETURN_IF_FAIL_TO_RETAIN_WEAK_PTR (Recordset, rs_ptr, rs)

  try
  {
    RecMutexLock usr_dbc_conn_mutex= ensure_valid_usr_connection();

    // we need transaction to enforce atomicity of change set
    // so if autocommit is currently enabled disable it temporarily
    bool auto_commit= _usr_dbc_conn->ref->getAutoCommit();
    ScopeExitTrigger autocommit_mode_keeper;
    int res= -2;
    if (!auto_commit)
    {
      int res= mforms::Utilities::show_warning(
        _("Apply Changes to Recordset"),
        _("Autocommit is currently disabled. Do you want to perform a COMMIT before applying the changes?\n"
          "If you do not commit, a failure during the recordset update will result in a rollback of the active transaction, if you have one."),
        _("Commit and Apply"),
        _("Cancel"),
        _("Apply"));

      if (res == mforms::ResultOk)
      {
        _usr_dbc_conn->ref->commit();
      }
    }
    else
    {
      autocommit_mode_keeper.slot= boost::bind(
        &sql::Connection::setAutoCommit, _usr_dbc_conn->ref.get(), 
        auto_commit);
      _usr_dbc_conn->ref->setAutoCommit(false);
    }

    if (res != mforms::ResultCancel) // only if not canceled
    {
      on_sql_script_run_error.disconnect_all_slots();
      on_sql_script_run_progress.disconnect_all_slots();
      on_sql_script_run_statistics.disconnect_all_slots();
      
      Recordset_data_storage::Ref data_storage_ref= rs->data_storage();
      Recordset_sql_storage *sql_storage= dynamic_cast<Recordset_sql_storage *>(data_storage_ref.get());
      
      scoped_connection c1(on_sql_script_run_error.connect(boost::bind(&SqlEditorForm::add_log_message, this, DbSqlEditorLog::ErrorMsg, _2, _3, "")));
      
      bool is_data_changes_commit_wizard_enabled= (0 != _grtm->get_app_option_int("DbSqlEditor:IsDataChangesCommitWizardEnabled", 1));
      if (is_data_changes_commit_wizard_enabled)
      {
        run_data_changes_commit_wizard(rs_ptr);
      }
      else
      {
        sql_storage->is_sql_script_substitute_enabled(false);
        
        scoped_connection on_sql_script_run_error_conn(sql_storage->on_sql_script_run_error.connect(on_sql_script_run_error));
        rs->do_apply_changes(_grtm->get_grt(), rs_ptr, Recordset_data_storage::Ptr(data_storage_ref));
      }
      
      // Since many messages could have been added it is possible the
      // the action log has not been refresh, this triggers a refresh
      refresh_log_messages(true);
    }
  }
  CATCH_ANY_EXCEPTION_AND_DISPATCH(_("Apply changes to recordset"))
}


bool SqlEditorForm::run_data_changes_commit_wizard(Recordset::Ptr rs_ptr)
{
  RETVAL_IF_FAIL_TO_RETAIN_WEAK_PTR (Recordset, rs_ptr, rs, false)

  // set underlying recordset data storage to use sql substitute (potentially modified by user)
  // instead of generating sql based on swap db contents
  Recordset_data_storage::Ref data_storage_ref= rs->data_storage();
  Recordset_sql_storage *sql_storage= dynamic_cast<Recordset_sql_storage *>(data_storage_ref.get());
  if (!sql_storage)
    return false;
  sql_storage->init_sql_script_substitute(rs_ptr, true);
  sql_storage->is_sql_script_substitute_enabled(true);
  const Sql_script &sql_script= sql_storage->sql_script_substitute();;
  std::string sql_script_text= Recordset_sql_storage::statements_as_sql_script(sql_script.statements);

  // No need for online DDL settings or callback as we are dealing with data here, not metadata.
  SqlScriptRunWizard wizard(_grtm, rdbms_version(), "", "");

  scoped_connection c1(on_sql_script_run_error.connect(boost::bind(&SqlScriptApplyPage::on_error, wizard.apply_page, _1, _2, _3)));
  scoped_connection c2(on_sql_script_run_progress.connect(boost::bind(&SqlScriptApplyPage::on_exec_progress, wizard.apply_page, _1)));
  scoped_connection c3(on_sql_script_run_statistics.connect(boost::bind(&SqlScriptApplyPage::on_exec_stat, wizard.apply_page, _1, _2)));
  wizard.values().gset("sql_script", sql_script_text);
  wizard.apply_page->apply_sql_script= boost::bind(&SqlEditorForm::apply_data_changes_commit, this, _1, rs_ptr);
  wizard.run_modal();

  return !wizard.has_errors();
}


void SqlEditorForm::apply_object_alter_script(std::string &alter_script, bec::DBObjectEditorBE* obj_editor, RowId log_id)
{
  set_log_message(log_id, DbSqlEditorLog::BusyMsg, "", 
                          obj_editor ? strfmt(_("Applying changes to %s..."), obj_editor->get_name().c_str()) : _("Applying changes..."), "");
  
  SqlFacade::Ref sql_splitter= SqlFacade::instance_for_rdbms(rdbms());
  std::list<std::string> statements;
  sql_splitter->splitSqlScript(alter_script, statements);
  
  int max_query_size_to_log = _grtm->get_app_option_int("DbSqlEditor:MaxQuerySizeToHistory", 0);
  
  std::list<std::string> failback_statements;
  if (obj_editor)
  {
    // in case of alter script failure:
    // try to restore object since it could had been successfully dropped before the alter script failed
    db_DatabaseObjectRef db_object= obj_editor->get_dbobject();
    std::string original_object_ddl_script= db_object->customData().get_string("originalObjectDDL", "");
    if (!original_object_ddl_script.empty())
    {
      // reuse the setting schema statement which is the first statement of the alter script
      std::string sql= *statements.begin();
      if ((0 == sql.find("use")) || (0 == sql.find("USE")))
        failback_statements.push_back(sql);
      sql_splitter->splitSqlScript(original_object_ddl_script, failback_statements);
    }
  }
  
  sql::SqlBatchExec sql_batch_exec;
  sql_batch_exec.stop_on_error(true);
  sql_batch_exec.failback_statements(failback_statements);
  
  sql_batch_exec.error_cb(boost::ref(on_sql_script_run_error));
  sql_batch_exec.batch_exec_progress_cb(boost::ref(on_sql_script_run_progress));
  sql_batch_exec.batch_exec_stat_cb(boost::ref(on_sql_script_run_statistics));
  
  /*
   if (obj_editor)
   {
   on_sql_script_run_error.connect(obj_editor->on_live_object_change_error);
   on_sql_script_run_progress.connect(obj_editor->on_live_object_change_progress);
   on_sql_script_run_statistics.connect(obj_editor->on_live_object_change_statistics);
   }*/
  
  long sql_batch_exec_err_count= 0;
  {
    try
    {
      RecMutexLock aux_dbc_conn_mutex(ensure_valid_aux_connection());
      std::auto_ptr<sql::Statement> stmt(_aux_dbc_conn->ref->createStatement());
      sql_batch_exec_err_count= sql_batch_exec(stmt.get(), statements);
    }
    catch (sql::SQLException &e)
    {
      set_log_message(log_id, DbSqlEditorLog::ErrorMsg, strfmt(SQL_EXCEPTION_MSG_FORMAT, e.getErrorCode(), e.what()), strfmt(_("Apply ALTER script for %s"), obj_editor->get_name().c_str()), "");
    }
    catch (std::exception &e)
    {
      set_log_message(log_id, DbSqlEditorLog::ErrorMsg, strfmt(EXCEPTION_MSG_FORMAT, e.what()), strfmt(_("Apply ALTER script for %s"), obj_editor->get_name().c_str()), "");
    }
  }
  
  if (!max_query_size_to_log || max_query_size_to_log >= (int)alter_script.size() )
    _history->add_entry(sql_batch_exec.sql_log());
  
  // refresh object's state only on success, to not lose changes made by user
  if (obj_editor && (0 == sql_batch_exec_err_count))
  {
    db_DatabaseObjectRef db_object= obj_editor->get_dbobject();
    
    set_log_message(log_id, DbSqlEditorLog::OKMsg, strfmt(_("Changes applied to %s"), obj_editor->get_name().c_str()), "", "");
    
    // refresh state of created/altered object in physical overview
    {
      std::string schema_name= db_SchemaRef::can_wrap(db_object) ? std::string() : *db_object->owner()->name();
      db_SchemaRef schema;
      if (!schema_name.empty())
        schema= db_SchemaRef::cast_from(db_object->owner());
      
      wb::LiveSchemaTree::ObjectType db_object_type = wb::LiveSchemaTree::Any;
      if (db_SchemaRef::can_wrap(db_object))
        db_object_type= wb::LiveSchemaTree::Schema;
      else if (db_TableRef::can_wrap(db_object))
        db_object_type= wb::LiveSchemaTree::Table;
      else if (db_ViewRef::can_wrap(db_object))
        db_object_type= wb::LiveSchemaTree::View;
      else if (db_RoutineRef::can_wrap(db_object))
      {
        db_RoutineRef db_routine = db_RoutineRef::cast_from(db_object);

        std::string obj_type = db_routine->routineType();

        if (obj_type == "function")
          db_object_type= wb::LiveSchemaTree::Function;
        else
          db_object_type= wb::LiveSchemaTree::Procedure;
      }
      
      _live_tree->refresh_live_object_in_overview(db_object_type, schema_name, db_object->oldName(), db_object->name());
    }
    
    _live_tree->refresh_live_object_in_editor(obj_editor, false);
  }
}

void SqlEditorForm::apply_data_changes_commit(std::string &sql_script_text, Recordset::Ptr rs_ptr)
{
  RETURN_IF_FAIL_TO_RETAIN_WEAK_PTR (Recordset, rs_ptr, rs);

  // this lock is supposed to be acquired lower in call-stack by SqlEditorForm::apply_changes_to_recordset
  //MutexLock usr_conn_mutex= ensure_valid_usr_connection();

  Recordset_data_storage::Ref data_storage_ref= rs->data_storage();
  Recordset_sql_storage *sql_storage= dynamic_cast<Recordset_sql_storage *>(data_storage_ref.get());
  if (!sql_storage)
    return;

  int max_query_size_to_log = _grtm->get_app_option_int("DbSqlEditor:MaxQuerySizeToHistory", 0);

  Sql_script sql_script= sql_storage->sql_script_substitute();
  sql_script.statements.clear();
  SqlFacade::Ref sql_splitter= SqlFacade::instance_for_rdbms(rdbms());
  sql_splitter->splitSqlScript(sql_script_text, sql_script.statements);

  scoped_connection on_sql_script_run_error_conn(sql_storage->on_sql_script_run_error.connect(on_sql_script_run_error));
  scoped_connection on_sql_script_run_progress_conn(sql_storage->on_sql_script_run_progress.connect(on_sql_script_run_progress));
  scoped_connection on_sql_script_run_statistics_conn(sql_storage->on_sql_script_run_statistics.connect(on_sql_script_run_statistics));

  sql_storage->sql_script_substitute(sql_script);
  rs->do_apply_changes(_grtm->get_grt(), rs_ptr, Recordset_data_storage::Ptr(data_storage_ref));

  if (!max_query_size_to_log || max_query_size_to_log >= (int)sql_script_text.size() )
    _history->add_entry(sql_script.statements);
}


std::string SqlEditorForm::active_schema() const
{
  return (_usr_dbc_conn) ? _usr_dbc_conn->active_schema : std::string();
}

/**
 * Notification from the tree controller that schema meta data has been refreshed. We use this
 * info to update the auto completion cache - avoiding so a separate set of queries to the server.
 */
void SqlEditorForm::schema_meta_data_refreshed(const std::string &schema_name,
 const std::vector<std::pair<std::string,bool> >& tables, 
 const std::vector<std::pair<std::string,bool> >& procedures, bool just_append)
{
  if (_auto_completion_cache != NULL)
  {
    _auto_completion_cache->update_schema_tables(schema_name, tables, just_append);

    // Schedule a refresh of column info for all tables/views.
    for (std::vector<std::pair<std::string, bool> >::const_iterator iterator = tables.begin(); iterator != tables.end(); ++iterator)
      _auto_completion_cache->refresh_table_cache(schema_name, iterator->first);

    _auto_completion_cache->update_schema_routines(schema_name, procedures, just_append);
  }
}

void SqlEditorForm::cache_active_schema_name()
{
  std::string schema=_usr_dbc_conn->ref->getSchema();
  _usr_dbc_conn->active_schema= schema;
  _aux_dbc_conn->active_schema= schema;

  if(_auto_completion_cache)
    _auto_completion_cache->refresh_schema_cache_if_needed(schema);
  
  exec_sql_task->execute_in_main_thread(
      boost::bind(&SqlEditorForm::update_editor_title_schema, this, schema),
      false,
      true);
}


void SqlEditorForm::active_schema(const std::string &value)
{
  try
  {
    if (value == active_schema())
      return;

    if (_auto_completion_cache)
      _auto_completion_cache->refresh_schema_cache_if_needed(value);
  
    {
      RecMutexLock aux_dbc_conn_mutex(ensure_valid_aux_connection());
      if (!value.empty())
        _aux_dbc_conn->ref->setSchema(value);
      _aux_dbc_conn->active_schema= value;
    }

    {
      RecMutexLock usr_dbc_conn_mutex(ensure_valid_usr_connection());
      if (!value.empty())
        _usr_dbc_conn->ref->setSchema(value);
      _usr_dbc_conn->active_schema= value;
    }

    // set current schema for the editors to notify the autocompleter
    for (Sql_editors::const_iterator ed = _sql_editors.begin(); ed != _sql_editors.end(); ++ed)
    {
      (*ed)->editor->set_current_schema(value);
    }

    _live_tree->on_active_schema_change(value);
    // remember active schema
    _connection->parameterValues().gset("DbSqlEditor:LastDefaultSchema", value);

    update_editor_title_schema(value);
    
    if (value.empty())
      grt_manager()->replace_status_text(_("Active schema was cleared"));
    else
      grt_manager()->replace_status_text(strfmt(_("Active schema changed to %s"), value.c_str()));
    
    _grtm->get_grt()->call_module_function("Workbench", "saveConnections", grt::BaseListRef());
  }
  CATCH_ANY_EXCEPTION_AND_DISPATCH(_("Set active schema"))
}


db_mgmt_RdbmsRef SqlEditorForm::rdbms()
{
  if (_connection.is_valid())
  {
    if (!_connection->driver().is_valid())
      throw std::runtime_error("Connection has invalid driver, check connection parameters.");
    return db_mgmt_RdbmsRef::cast_from(_connection->driver()->owner());
  }
  else
    return db_mgmt_RdbmsRef::cast_from(_grtm->get_grt()->get("/wb/doc/physicalModels/0/rdbms"));
}


int SqlEditorForm::count_connection_editors(const std::string &conn_name)
{
  int count = 0;
  boost::weak_ptr<SqlEditorForm> editor;
  
  std::list<boost::weak_ptr<SqlEditorForm> >::iterator index, end;
  
  end = _wbsql->get_open_editors()->end();
  for(index = _wbsql->get_open_editors()->begin(); index != end; index++)
  {
    SqlEditorForm::Ref editor((*index).lock());
    std::string editor_connection = editor->_connection->name();
    if (editor_connection == conn_name)
      count++;    
  }
  
  return count;  
}

//--------------------------------------------------------------------------------------------------

std::string SqlEditorForm::create_title()
{
  std::string caption;
  std::string editor_connection = get_session_name();
  
  if (!editor_connection.empty())
    caption += strfmt("%s", editor_connection.c_str());
  else
  {
    if (_connection->driver()->name() == "MysqlNativeSocket")
      caption += "localhost";
    else
      caption+= strfmt("%s", truncate_text(editor_connection,21).c_str());
  }

  // only show schema name if there's more than 1 tab to the same connection, to save space
  if (!_usr_dbc_conn->active_schema.empty() && count_connection_editors(editor_connection) > 1)
    caption += strfmt(" (%s)", truncate_text(_usr_dbc_conn->active_schema, 20).c_str());

  if (_connection_details.find("dbmsProductVersion") != _connection_details.end()
      && !bec::is_supported_mysql_version(_connection_details["dbmsProductVersion"]))
    caption += " - Warning - not supported";
  
  return caption;
}

//--------------------------------------------------------------------------------------------------

void SqlEditorForm::update_title()
{
  std::string temp_title = create_title();
  if (_title != temp_title)
  {
    _title = temp_title;
    title_changed();
  }
}

//--------------------------------------------------------------------------------------------------

GrtVersionRef SqlEditorForm::rdbms_version() const
{
  return _version;
}

//--------------------------------------------------------------------------------------------------

/**
 * Returns the current server version (or a reasonable default if not connected) in compact form
 * as needed for parsing on various occasions (context help, auto completion, error parsing).
 */
int SqlEditorForm::server_version()
{
  GrtVersionRef version = rdbms_version();

  // Create a server version of the form "Mmmrr" as long int for quick comparisons.
  if (version.is_valid())
    return (int)(version->majorNumber() * 10000 + version->minorNumber() * 100 + version->releaseNumber());
  else
    return 50503;
}

//--------------------------------------------------------------------------------------------------

/**
 * Returns a list of valid charsets for this connection as needed for parsing.
 */
std::set<std::string> SqlEditorForm::valid_charsets()
{
  if (_charsets.empty())
  {
    grt::ListRef<db_CharacterSet> list = rdbms()->characterSets();
    for (size_t i = 0; i < list->count(); i++)
      _charsets.insert(base::tolower(*list[i]->name()));

    // 3 character sets were added in version 5.5.3. Remove them from the list if the current version
    // is lower than that.
    if (server_version() < 50503)
    {
      _charsets.erase("utf8mb4");
      _charsets.erase("utf16");
      _charsets.erase("utf32");
    }
  }
  return _charsets;
}

//--------------------------------------------------------------------------------------------------

bool SqlEditorForm::save_snippet()
{
  MySQLEditor::Ref editor = active_sql_editor();
  if (!editor)
    return false;
  std::string text;
  size_t start, end;
  if (editor->selected_range(start, end))
    text = editor->selected_text();
  else
    text = editor->current_statement();

  if (text.empty())
    return false;

  DbSqlEditorSnippets::get_instance()->add_snippet("", text, true);
  _grtm->replace_status_text("SQL saved to snippets list.");

  _side_palette->refresh_snippets();

  return true;
}

//--------------------------------------------------------------------------------------------------

bool SqlEditorForm::can_close()
{  
  return can_close_(true);
}

bool SqlEditorForm::can_close_(bool interactive)
{
  if (exec_sql_task && exec_sql_task->is_busy())
  {
    _grtm->replace_status_text(_("Cannot close SQL IDE while being busy"));
    return false;
  }

  if (!bec::UIForm::can_close())
    return false;

  _live_tree->prepare_close();
  _grtm->set_app_option("DbSqlEditor:ActiveSidePaletteTab", grt::IntegerRef(_side_palette->get_active_tab()));
  
  bool check_scratch_editors = true;
  bool save_workspace_on_close = false;

  // if Save of workspace on close is enabled, we don't need to check whether there are unsaved
  // SQL editors but other stuff should be checked.
  grt::ValueRef option(_grtm->get_app_option("workbench:SaveSQLWorkspaceOnClose"));
  if (option.is_valid() && *grt::IntegerRef::cast_from(option))
  {
    save_workspace_on_close = true;
    check_scratch_editors = false;
  }
  bool editor_needs_review = false;
  if (interactive)
  {
    ConfirmSaveDialog dialog(0, "Close SQL Editor", "The following files/resultsets have unsaved changes.\nDo you want to review these changes before closing?");
    for (int i = 0; i < sql_editor_count(); i++)
    {
      bool check_editor = !sql_editor_is_scratch(i) || check_scratch_editors;
      if (sql_editor_path(i).empty() && save_workspace_on_close)
        check_editor = false;

      if (sql_editor(i)->get_editor_control()->is_dirty() && check_editor)
      {
        editor_needs_review = true;

        std::string n= sql_editor_path(i);
        if (!n.empty())
          n= base::basename(n).append(" - ").append(n);
        else
          n= "Unsaved SQL Query";
        dialog.add_item("Script Buffers", n);
      }

      RecordsetsRef rsets = sql_editor_recordsets(i);
      BOOST_FOREACH (Recordset::Ref rs, *(rsets.get()))
      {
        if (!rs->can_close(false))
          dialog.add_item("Resultset", rs->caption());
      }
    }
    
    bool review= false;
    if (dialog.change_count() > 1)
    {
      switch (dialog.run()) 
      {
        case ConfirmSaveDialog::ReviewChanges:
          review= true;
          break;

        case ConfirmSaveDialog::DiscardChanges:
          review= false;
          break;

        case ConfirmSaveDialog::Cancel:
          return false;
      }
    }
    else if (dialog.change_count() == 1)
      review= true;
     
    // review changes 1 by 1
    if (review && editor_needs_review)
    {
      for (int i = 0; i < sql_editor_count(); i++)
      {
        // sql_editor_will_close() checks for unsaved recordsets
        if (!sql_editor_will_close(i))
          return false;
      }
    }
  }
  else // !interactive
  {
    for (int i = 0; i < sql_editor_count(); i++)
    {
      if (editor_needs_review && sql_editor(i)->get_editor_control()->is_dirty())
        return false;

      {
        MutexTryLock lock(_sql_editors[i]->recordset_mutex);
        if (!lock.locked())
          return false;

        RecordsetsRef rsets = sql_editor_recordsets(i);
        BOOST_FOREACH (Recordset::Ref rs, *(rsets.get()))
        {
          if (!rs->can_close(false))
            return false;
        }
      }
    }
  }

  return true;
}


void SqlEditorForm::check_external_file_changes()
{
  for (int i = 0; i < sql_editor_count(); i++)
  {
    if (!_sql_editors[i]->filename.empty())
    {
      time_t ts;
      if (base::file_mtime(_sql_editors[i]->filename, ts))
      {
        if (ts > _sql_editors[i]->file_timestamp)
        {
          // File was changed externally. For now we ignore local changes if the user chooses to reload.
          std::string connection_description = connection_descriptor().is_valid() ?
            base::strfmt("(from connection to %s) ", connection_descriptor()->name().c_str()) : "";
          if (mforms::Utilities::show_warning("File Changed",
            base::strfmt(_("File %s %swas changed from outside MySQL Workbench.\nWould you like to discard your changes and reload it?"),
              _sql_editors[i]->filename.c_str(), connection_description.c_str()),
              "Reload File", "Ignore", "") == mforms::ResultOk
            )
          {
            _sql_editors[i]->editor->sql("");
            sql_editor_open_file(i, _sql_editors[i]->filename, _sql_editors[i]->orig_encoding);
          }
          else
          {
           // _sql_editors[i]->editor->get_editor_control()->set_text(_sql_editors[i]->editor->get_editor_control()->get_text(false).data());
            _sql_editors[i]->file_timestamp = ts;
          }
        }
      }
    }
  }
}

//--------------------------------------------------------------------------------------------------

void SqlEditorForm::update_editor_title_schema(const std::string& schema)
{
  _live_tree->on_active_schema_change(schema);
  
  // Gets the editor label including the schema name only if
  // the number of opened editors to the same host is > 1
  update_title();
}

//--------------------------------------------------------------------------------------------------

/* Called whenever a connection to a server is opened, whether it succeeds or not.


 Call this when a connection to the server is opened. If the connection succeeded, pass 0 as
 the error and if it fails, pass the error code.

 The error will be used to determine whether the connection failed because the server is possibly
 down (or doesn't exist) or some other reason (like wrong password).
 */
void SqlEditorForm::note_connection_open_outcome(int error)
{
  ServerState new_state;
  switch (error)
  {
    case 0:
      new_state = RunningState; // success = running;
      break;
    case 2002: // CR_CONNECTION_ERROR
    case 2003: // CR_CONN_HOST_ERROR
      new_state = PossiblyStoppedState;
      break;
    default:
      // there may be other errors that could indicate server stopped and maybe
      // some errors that can't tell anything about the server state
      new_state = RunningState;
      break;
  }

  if (_last_server_running_state != new_state && new_state != UnknownState)
  {
    grt::DictRef info(_grtm->get_grt());
    _last_server_running_state = new_state;

    info.gset("state", new_state == RunningState);
    info.set("connection", connection_descriptor());

    log_debug("Notifying server state change of %s to %s\n", connection_descriptor()->hostIdentifier().c_str(), new_state == RunningState ? "running" : "not running");
    GRTNotificationCenter::get()->send_grt("GRNServerStateChanged",
                                           _wbsql->get_grt_editor_object(this),
                                           info);
  }
}

//--------------------------------------------------------------------------------------------------
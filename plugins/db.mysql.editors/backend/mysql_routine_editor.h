/* 
 * Copyright (c) 2009, 2014, Oracle and/or its affiliates. All rights reserved.
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

#pragma once

#include "grtdb/editor_routine.h"

#include "mysql_support_backend_public_interface.h"

class MYSQLWBMYSQLSUPPORTBACKEND_PUBLIC_FUNC MySQLRoutineEditorBE : public bec::RoutineEditorBE
{
public:
  MySQLRoutineEditorBE(bec::GRTManager *grtm, const db_mysql_RoutineRef &routine); 

  virtual db_RoutineRef get_routine() { return _routine; }
  
  void load_routine_sql();
  virtual void commit_changes();
  virtual bool can_close();

private:
  db_mysql_RoutineRef _routine;
};
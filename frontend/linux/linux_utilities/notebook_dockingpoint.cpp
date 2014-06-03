/* 
 * Copyright (c) 2012, 2014, Oracle and/or its affiliates. All rights reserved.
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

#include "notebook_dockingpoint.h"
#include "active_label.h"
#include "gtk/lf_mforms.h"
#include "gtk/lf_view.h"
#include "base/log.h"
DEFAULT_LOG_DOMAIN("notebook_dockingpoint")

void NotebookDockingPoint::close_appview_page(mforms::AppView *view)
{
  _dpoint->close_view(view);
}

bool NotebookDockingPoint::close_page(Gtk::Widget *w)
{
  mforms::AppView *aview = dynamic_cast<mforms::AppView*>(mforms::gtk::ViewImpl::get_view_for_widget(w));
  if (aview)
    return _dpoint->close_view(aview);
  return true;
}


void NotebookDockingPoint::dock_view(mforms::AppView *view, const std::string &arg1, int arg2)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    ActiveLabel *l = Gtk::manage(new ActiveLabel("mforms", sigc::bind(sigc::mem_fun(this, &NotebookDockingPoint::close_appview_page), view)));
    int i = _notebook->append_page(*w, *l);
    _notebook->set_current_page(i);
    w->set_data("NotebookDockingPoint:label", l);

    notebook_changed_signal.emit(true);
  }
}

bool NotebookDockingPoint::select_view(mforms::AppView *view)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    int p = _notebook->page_num(*w);
    if (p >= 0)
    {
      _notebook->set_current_page(p);
      return true;
    }
  }
  return false;
}

void NotebookDockingPoint::undock_view(mforms::AppView *view)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    //before remove, unset menu if it was set
    _notebook->remove_page(*w);
    notebook_changed_signal.emit(false);
  }
}

void NotebookDockingPoint::set_view_title(mforms::AppView *view, const std::string &title)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    int idx = _notebook->page_num(*w);
    if (idx >= 0)
    {
      Gtk::Widget *page = _notebook->get_nth_page(idx);
      if (page)
      {
        ActiveLabel *label = reinterpret_cast<ActiveLabel*>(page->get_data("NotebookDockingPoint:label"));
        if (label)
          label->set_text(title);
      }
    }
    else
      g_warning("Can't set title of unknown view to %s", title.c_str());
  }
}

std::pair<int, int> NotebookDockingPoint::get_size()
{
  return std::pair<int, int>(_notebook->get_width(), _notebook->get_height());
}


void NotebookDockingPoint::set_notebook(Gtk::Notebook *note)
{
  _notebook = note;
}


void ActionAreaNotebookDockingPoint::set_notebook(ActionAreaNotebook *note)
{
  _notebook = note;
}


void ActionAreaNotebookDockingPoint::close_page(mforms::AppView *view)
{
  _dpoint->close_view(view);
}


void ActionAreaNotebookDockingPoint::dock_view(mforms::AppView *view, const std::string &arg1, int arg2)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    ActiveLabel *l = Gtk::manage(new ActiveLabel("mforms", sigc::bind(sigc::mem_fun(this, &ActionAreaNotebookDockingPoint::close_page), view)));
    int num = _notebook->append_page(*w, *l);
    _notebook->set_current_page(num);
    w->set_data("ActionAreaNotebookDockingPoint:label", l);

    notebook_changed_signal.emit(true);
  }
}

bool ActionAreaNotebookDockingPoint::select_view(mforms::AppView *view)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    int p = _notebook->page_num(*w);
    if (p >= 0)
    {
      _notebook->set_current_page(p);
      return true;
    }
  }
  return false;
}

void ActionAreaNotebookDockingPoint::undock_view(mforms::AppView *view)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    _notebook->remove_page(*w);
    notebook_changed_signal.emit(false);
  }
}

void ActionAreaNotebookDockingPoint::set_view_title(mforms::AppView *view, const std::string &title)
{
  Gtk::Widget *w = mforms::widget_for_view(view);
  if (w)
  {
    int idx = _notebook->page_num(*w);
    if (idx >=0)
    {
      Gtk::Widget *page = _notebook->get_nth_page(idx);
      if (page)
      {
        ActiveLabel *label = reinterpret_cast<ActiveLabel*>(page->get_data("ActionAreaNotebookDockingPoint:label"));
        if (label)
          label->set_text(title);
      }
    }
    else
      log_warning("Can't set title of unknown view to %s\n", title.c_str());
  }
}

std::pair<int, int> ActionAreaNotebookDockingPoint::get_size()
{
  return std::pair<int, int>(_notebook->get_width(), _notebook->get_height());
}


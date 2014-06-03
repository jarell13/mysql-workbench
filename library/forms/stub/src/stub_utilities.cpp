/* 
 * Copyright (c) 2008, 2014, Oracle and/or its affiliates. All rights reserved.
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
 
#include "../stub_utilities.h"

#include "base/log.h"
#include <cerrno>
using namespace mforms;
using namespace stub;

boost::function<mforms::DialogResult (void)> UtilitiesWrapper::message_callback;
std::map<const std::string, std::string> UtilitiesWrapper::passwords;

DEFAULT_LOG_DOMAIN("mforms");

int UtilitiesWrapper::show_message(const std::string &title, const std::string &text,
                        const std::string &ok, const std::string &cancel,
                        const std::string &other)
{
  log_info("DIALOG: %s: %s\n", title.c_str(), text.c_str());
  if (message_callback)
    return message_callback();

  if (other == "Don't Save")
      return mforms::ResultOther;
  return mforms::ResultOk;
}


int UtilitiesWrapper::show_error(const std::string &title, const std::string &text,
                      const std::string &ok, const std::string &cancel,
                      const std::string &other)
{
  log_info("DIALOG: %s: %s\n", title.c_str(), text.c_str());
  if (message_callback)
    return message_callback();

  return  mforms::ResultOk;
}

int UtilitiesWrapper::show_warning(const std::string &title, const std::string &text,
                      const std::string &ok, const std::string &cancel,
                      const std::string &other)
{
  log_info("DIALOG: %s: %s\n", title.c_str(), text.c_str());
  if (message_callback)
    return message_callback();

  return  mforms::ResultOk;
}

int UtilitiesWrapper::show_message_with_checkbox(const std::string &title, const std::string &text,
                      const std::string &ok, const std::string &cancel,
                      const std::string &other,
                      const std::string &checkbox_text, // empty text = default "Don't show this message again" text
                      bool &remember_checked)
{
  log_info("DIALOG: %s: %s\n", title.c_str(), text.c_str());
  if (message_callback)
    return message_callback();

  return  mforms::ResultOk;
}


void UtilitiesWrapper::show_wait_message(const std::string &title, const std::string &text)
{
}

bool UtilitiesWrapper::hide_wait_message()
{
  return true;
}

bool UtilitiesWrapper::run_cancelable_wait_message(const std::string &title, const std::string &text,
                      const boost::function<void ()> &start_task, const boost::function<bool ()> &cancel_task)
{
  return true;
}

void UtilitiesWrapper::stop_cancelable_wait_message()
{
}

    
void UtilitiesWrapper::set_clipboard_text(const std::string &text)
{
}

std::string UtilitiesWrapper::get_clipboard_text()
{
  return "";
}


void UtilitiesWrapper::open_url(const std::string &url)
{
}

std::string UtilitiesWrapper::get_special_folder(mforms::FolderType type)
{
  return "./";
}

mforms::TimeoutHandle UtilitiesWrapper::add_timeout(float interval, const boost::function<bool ()> &slot)
{
  return 0;
}

void UtilitiesWrapper::cancel_timeout(mforms::TimeoutHandle)
{
}

void UtilitiesWrapper::store_password(const std::string &service, const std::string &account, const std::string &password)
{
  passwords[service+":"+account] = password;
}

//--------------------------------------------------------------------------------------------------

bool UtilitiesWrapper::find_password(const std::string &service, const std::string &account, std::string &password)
{
  static bool loaded_passwords = false;
  bool ret_val = false;

  if (!loaded_passwords)
  {
    char *password_file = getenv("TEST_PASSWORD_FILE");
    if (!password_file)
    {
      g_message("Specify a password file for tests with the TEST_PASSWORD_FILE env variable.");
    }
    else
    {
      FILE *f = fopen(password_file, "r");
      if (f)
      {
        if (getenv("VERBOSE"))
          g_message("Loading %s", password_file);
        char line[1024];
        while ((fgets(line, sizeof(line), f)))
        {
          char *tmp = strrchr(line, '=');
          if (tmp)
          {
            char *e = strchr(tmp, '\r');
            if (e) *e = 0;
            e = strchr(tmp, '\n');
            if (e) *e = 0;
            passwords[std::string(line, tmp-line)] = tmp+1;
            if (getenv("VERBOSE"))
              g_message("%s=%s", std::string(line, tmp-line).c_str(), tmp+1);
          }
        }
      }
      else
        log_error("Could not open password file %s: %i\n", password_file, errno);
    }
    loaded_passwords = true;
  }

  if (passwords.count(service+":"+account))
  {
    password = passwords[service+":"+account];
    ret_val = true;
  }
  else
  {
    if (passwords.count(":"+account))
    {
      password = passwords[":"+account];
      ret_val = true;
    }
    else
      log_error("Unknown password for %s:%s\n", service.c_str(), account.c_str());
  }
  return ret_val;
}

//--------------------------------------------------------------------------------------------------

void UtilitiesWrapper::forget_password(const std::string &service, const std::string &account)
{
}

//--------------------------------------------------------------------------------------------------

enum {Gnome_keyring_results_size = 10};
static const char* gnome_keyring_results[Gnome_keyring_results_size] = {"OK",
                                             "GNOME_KEYRING_RESULT_DENIED",
                                             "GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON",
                                             "GNOME_KEYRING_RESULT_ALREADY_UNLOCKED",
                                             "GNOME_KEYRING_RESULT_NO_SUCH_KEYRING",
                                             "GNOME_KEYRING_RESULT_BAD_ARGUMENTS",
                                             "GNOME_KEYRING_RESULT_IO_ERROR",
                                             "GNOME_KEYRING_RESULT_CANCELLED",
                                             "GNOME_KEYRING_RESULT_ALREADY_EXISTS",
                                             ""
                                            };
//--------------------------------------------------------------------------------------------------

void* UtilitiesWrapper::perform_from_main_thread(const boost::function<void* ()>& slot, bool wait)
{
  return slot();
};

//--------------------------------------------------------------------------------------------------

void UtilitiesWrapper::init()
{
  ::mforms::ControlFactory *f = ::mforms::ControlFactory::get_instance();

  f->_utilities_impl.show_message= &UtilitiesWrapper::show_message;
  f->_utilities_impl.show_error= &UtilitiesWrapper::show_error;
  f->_utilities_impl.show_warning= &UtilitiesWrapper::show_warning;
  f->_utilities_impl.set_clipboard_text= &UtilitiesWrapper::set_clipboard_text;
  f->_utilities_impl.get_clipboard_text= &UtilitiesWrapper::get_clipboard_text;
  f->_utilities_impl.open_url= &UtilitiesWrapper::open_url;
  f->_utilities_impl.add_timeout= &UtilitiesWrapper::add_timeout;
  f->_utilities_impl.cancel_timeout= &UtilitiesWrapper::cancel_timeout;
  f->_utilities_impl.get_special_folder= &UtilitiesWrapper::get_special_folder;
  f->_utilities_impl.store_password= &UtilitiesWrapper::store_password;
  f->_utilities_impl.find_password= &UtilitiesWrapper::find_password;
  f->_utilities_impl.forget_password= &UtilitiesWrapper::forget_password;

  f->_utilities_impl.hide_wait_message= &UtilitiesWrapper::hide_wait_message;
  f->_utilities_impl.run_cancelable_wait_message= &UtilitiesWrapper::run_cancelable_wait_message;
  f->_utilities_impl.show_message_with_checkbox= &UtilitiesWrapper::show_message_with_checkbox;
  f->_utilities_impl.show_wait_message= &UtilitiesWrapper::show_wait_message;
  f->_utilities_impl.stop_cancelable_wait_message= &UtilitiesWrapper::stop_cancelable_wait_message;
  f->_utilities_impl.perform_from_main_thread= &UtilitiesWrapper::perform_from_main_thread;
}

//--------------------------------------------------------------------------------------------------

void UtilitiesWrapper::set_message_callback(boost::function<mforms::DialogResult (void)> callback)
{
  message_callback = callback;
}

//--------------------------------------------------------------------------------------------------
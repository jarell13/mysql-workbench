/* 
 * Copyright (c) 2010, 2014, Oracle and/or its affiliates. All rights reserved.
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

#include "grt/grt_manager.h"
#include "binary_data_editor.h"
#include "base/string_utilities.h"
#include <glib/gstdio.h>
#ifdef _WIN32
#include <io.h>
#endif

#include "mforms/scrollpanel.h"
#include "mforms/imagebox.h"
#include "mforms/treenodeview.h"
#include "mforms/code_editor.h"
#include "mforms/filechooser.h"

BinaryDataViewer::BinaryDataViewer(BinaryDataEditor *owner)
: mforms::Box(false), _owner(owner)
{
}

//--------------------------------------------------------------------------------

class ImageDataViewer : public BinaryDataViewer
{
public:
  ImageDataViewer(BinaryDataEditor *owner, bool read_only)
  : BinaryDataViewer(owner), _scroll(mforms::ScrollPanelNoFlags)
  {
    _image.set_scale_contents(false);
    add(&_scroll, true, true);
    _scroll.add(&_image);
  }

  virtual void data_changed()
  {
    _image.set_image_data(_owner->data(), _owner->length());
  }

private:
  mforms::ScrollPanel _scroll;
  mforms::ImageBox _image;
};

//--------------------------------------------------------------------------------

class HexDataViewer : public BinaryDataViewer
{
public:
  HexDataViewer(BinaryDataEditor *owner, bool read_only)
  : BinaryDataViewer(owner), _tree(mforms::TreeShowColumnLines|mforms::TreeShowRowLines|mforms::TreeFlatList), _box(true)
  {
    _offset = 0;
    _block_size = 8 * 1024;
    
    add(&_tree, true, true);
    add(&_box, false, true);

    _box.set_spacing(8);
    _box.add(&_first, false, true);
    _box.add(&_back, false, true);
    _box.add(&_next, false, true);
    _box.add(&_last, false, true);
    _box.add(&_label, true, true);
    
    _label.set_text("Viewing Range 0 to 16KB");
    _first.set_text("<< First");
    _back.set_text("< Previous");
    _next.set_text("Next >");
    _last.set_text("Last >>");
    scoped_connect(_first.signal_clicked(),boost::bind(&HexDataViewer::go, this, -2));
    scoped_connect(_back.signal_clicked(),boost::bind(&HexDataViewer::go, this, -1));
    scoped_connect(_next.signal_clicked(),boost::bind(&HexDataViewer::go, this, 1));
    scoped_connect(_last.signal_clicked(),boost::bind(&HexDataViewer::go, this, 2));
    
    _tree.add_column(mforms::StringColumnType, "Offset", 100, true);

    for (int i= 0; i < 16; i++)
      _tree.add_column(mforms::StringColumnType, base::strfmt("%X", i), 25, !read_only);
    _tree.end_columns();

    _tree.set_cell_edit_handler(boost::bind(&HexDataViewer::set_cell_value, this, _1, _2, _3));
  }

  virtual void data_changed()
  {
    if (_offset >= _owner->length())
      _offset = (_owner->length() / _block_size) * _block_size;
    
    refresh();
  }
 
  void go(int step)
  {
    switch (step)
    {
      case -2:
        _offset = 0;
        break;
      case -1:
        if (_block_size > _offset)
          _offset = 0;
        else
          _offset -= _block_size;
        break;
      case 1:
        _offset += _block_size;
        if (_offset >= _owner->length())
          _offset = (_owner->length()  / _block_size) * _block_size;
        break;
      case 2:
        _offset = (_owner->length()  / _block_size) * _block_size;
        break;
    }
    refresh();
  }

  void refresh()
  {
    suspend_layout();

    unsigned char *ptr = (unsigned char*)_owner->data() + _offset;
    _tree.clear();
    size_t offs;
    size_t length = std::min<size_t>(_offset + _block_size, _owner->length());
    for (offs = _offset; offs < length; offs += 16)
    {
      mforms::TreeNodeRef row = _tree.add_node();
      row->set_string(0, base::strfmt("0x%08x", (unsigned int) offs));

      for (size_t i= offs, min= std::min<size_t>(offs + 16, length); i < min; ++i)
      {
        row->set_string((int)(i + 1 - offs), base::strfmt("%02x", *ptr));
        ptr++;
      }
    }
    resume_layout();

    _label.set_text(base::strfmt("Viewing Range %i to %i", (int) _offset, (int) (_offset+_block_size)));
    
    if (_offset == 0)
    {
      _back.set_enabled(false);
      _first.set_enabled(false);
    }
    else
    {
      _back.set_enabled(true);
      _first.set_enabled(true);
    }
    if (_offset + _block_size < _owner->length()-1)
    {
      _next.set_enabled(true);
      _last.set_enabled(true);
    }
    else
    {
      _next.set_enabled(false);
      _last.set_enabled(false);
    }
  }
  
private:
  mforms::TreeNodeView _tree;
  mforms::Box _box;
  mforms::Button _first;
  mforms::Button _back;
  mforms::Label _label;
  mforms::Button _next;
  mforms::Button _last;
  size_t _offset;
  size_t _block_size;

  void set_cell_value(mforms::TreeNodeRef node, int column, const std::string &value)
  {
    size_t offset = _offset + _tree.row_for_node(node) * 16 + (column-1);
    
    if (offset < _owner->length())
    {
      int i;
      if (sscanf(value.c_str(), "%x", &i) != 1)
        return;
      if (i < 0 || i > 255)
        return;
      node->set_string(column, base::strfmt("%02x", i));
      
      *(unsigned char*)(_owner->data() + offset) = i;
      _owner->notify_edit();
    }
  }
};

//--------------------------------------------------------------------------------

class TextDataViewer : public BinaryDataViewer
{
public:
  TextDataViewer(BinaryDataEditor *owner, const std::string &encoding, bool read_only)
  : BinaryDataViewer(owner), _text(), _encoding(encoding)
  {
    if (_encoding.empty())
      _encoding = "UTF-8";
    
    add(&_message, false, true);
    add(&_text, true, true);
    
    _text.set_language(mforms::LanguageNone);
    _text.set_features(mforms::FeatureWrapText, true);
    _text.set_features(mforms::FeatureReadOnly, read_only);
    
    scoped_connect(_text.signal_changed(),boost::bind(&TextDataViewer::edited, this));
  }
  
  virtual void data_changed()
  {
    GError *error = 0;
    gchar *converted= NULL;
    gsize bread, bwritten;
    if (!_owner->data() || 
        !(converted = g_convert(_owner->data(), (gssize)_owner->length(), "UTF-8", _encoding.c_str(),
            &bread, &bwritten, &error)) ||
        _owner->length() != bread)
    {
      std::string message = "Data could not be converted to UTF-8 text";
      if (error)
      {
        message.append(": ").append(error->message);
        g_error_free(error);
      }
      g_free(converted);
      if (_owner->length())
      {
        _message.set_text(message);
        _text.set_features(mforms::FeatureReadOnly, true);
      }
      else
      {
        _text.set_features(mforms::FeatureReadOnly, false);
      }
      _text.set_value("");
    }
    else
    {
      _message.set_text("");
      _text.set_features(mforms::FeatureReadOnly, false);
      _text.set_value(std::string(converted, bwritten));
      if (_owner == NULL || _owner->read_only())
        _text.set_features(mforms::FeatureReadOnly, true);
    }

    if (converted != NULL)
      g_free(converted);
  }
  
private:
  mforms::CodeEditor _text;
  mforms::Label _message;
  std::string _encoding;

  void edited()
  {
    std::string data = _text.get_string_value();
    gchar *converted;
    gsize bread, bwritten;
    GError *error = 0;
    
    if (_encoding != "utf8" && _encoding != "UTF8" && _encoding != "utf-8" && _encoding != "UTF-8")
    {
      converted = g_convert(data.data(), (gssize)data.length(), _encoding.c_str(), "UTF-8", &bread,
        &bwritten, &error);
      if (converted == NULL || data.length() != bread)
      {
        std::string message = base::strfmt("Data could not be converted back to %s", _encoding.c_str());
        if (error)
        {
          message.append(": ").append(error->message);
          g_error_free(error);
        }
        _message.set_text(message);
        if (converted != NULL)
          g_free(converted);
        return;
      }
      
      _owner->assign_data(converted, bwritten);
      g_free(converted);
      _message.set_text("");
    }
    else
    {
      _owner->assign_data(data.data(), data.length());
      _message.set_text("");
    }
  }
};

//--------------------------------------------------------------------------------

BinaryDataEditor::BinaryDataEditor(bec::GRTManager *grtm, const char *data, size_t length, bool read_only)
: mforms::Form(0), _grtm(grtm), _box(false), _hbox(true), _read_only(read_only)
{

  set_name("blob_editor");
  _data = 0;
  _length = 0;

  grt::IntegerRef tab = grt::IntegerRef::cast_from(_grtm->get_app_option("BlobViewer:DefaultTab"));

  setup();
  assign_data(data, length);

  add_viewer(new HexDataViewer(this, read_only), "Binary");
  add_viewer(new TextDataViewer(this, "LATIN1", read_only), "Text");
  add_viewer(new ImageDataViewer(this, read_only), "Image");
    
  if (tab.is_valid())
    _tab_view.set_active_tab((int)*tab);
  tab_changed();
}

BinaryDataEditor::BinaryDataEditor(bec::GRTManager *grtm, const char *data, size_t length, const std::string &text_encoding, bool read_only)
: mforms::Form(0), _grtm(grtm), _box(false), _hbox(true), _read_only(read_only)
{
  set_name("blob_editor");
  _data = 0;
  _length = 0;

  grt::IntegerRef tab = grt::IntegerRef::cast_from(_grtm->get_app_option("BlobViewer:DefaultTab"));

  setup();
  assign_data(data, length);

  add_viewer(new HexDataViewer(this, read_only), "Binary");
  add_viewer(new TextDataViewer(this, text_encoding, read_only), "Text");
  add_viewer(new ImageDataViewer(this, read_only), "Image");
  
  if (tab.is_valid())
    _tab_view.set_active_tab((int)*tab);  
  tab_changed();
}

BinaryDataEditor::~BinaryDataEditor()
{
  g_free(_data);
}


void BinaryDataEditor::setup()
{
  set_title("Edit Data");
  set_size(640, 500);
  
  set_content(&_box);
  _box.set_padding(12);
  _box.set_spacing(12);
  
  _box.add(&_tab_view, true, true);
  _box.add(&_length_text, false, true);
  _box.add(&_hbox, false, true);

  _hbox.add(&_export, false, true);
  if (!_read_only)
    _hbox.add(&_import, false, true);
  
  if (!_read_only)
    _hbox.add_end(&_save, false, true);
  _hbox.add_end(&_close, false, true);
  _hbox.set_spacing(12);
  
  _save.set_text("Apply");
  _close.set_text("Close");
  _export.set_text("Save...");
  _import.set_text("Load...");
  
  scoped_connect(_tab_view.signal_tab_changed(),boost::bind(&BinaryDataEditor::tab_changed, this));
  
  scoped_connect(_save.signal_clicked(),boost::bind(&BinaryDataEditor::save, this));
  scoped_connect(_close.signal_clicked(),boost::bind(&BinaryDataEditor::close, this));
  scoped_connect(_import.signal_clicked(),boost::bind(&BinaryDataEditor::import_value, this));
  scoped_connect(_export.signal_clicked(),boost::bind(&BinaryDataEditor::export_value, this));
}

void BinaryDataEditor::notify_edit()
{
  _length_text.set_text(base::strfmt("Data Length: %i bytes", (int) _length));
}

void BinaryDataEditor::assign_data(const char *data, size_t length)
{
  if (data != _data)
  {
    g_free(_data);
    _data = (char*)g_memdup(data, (guint)length);
  }
  _length = length;

  _length_text.set_text(base::strfmt("Data Length: %i bytes", (int) _length));
}

void BinaryDataEditor::tab_changed()
{
  int i = _tab_view.get_active_tab();
  if (i < 0)
    i= 0;

  grt::DictRef dict(grt::DictRef::cast_from(_grtm->get_app_option("")));
  if (dict.is_valid())
    dict.gset("BlobViewer:DefaultTab", i);

  _viewers[i]->data_changed();
}

void BinaryDataEditor::add_viewer(BinaryDataViewer *viewer, const std::string &title)
{
  _viewers.push_back(viewer);
  
  _tab_view.add_page(viewer, title);
}

void BinaryDataEditor::save()
{
  signal_saved();
}

void BinaryDataEditor::import_value()
{
  mforms::FileChooser chooser(mforms::OpenFile);
  
  chooser.set_title("Import Field Data");
  if (chooser.run_modal())
  {
    std::string path = chooser.get_path();
    GError *error = 0;
    char *data;
    gsize length;
    
    if (!g_file_get_contents(path.c_str(), &data, &length, &error))
    {
      mforms::Utilities::show_error(base::strfmt("Could not import data from %s", path.c_str()),
                                    error->message, "OK");
      g_error_free(error);
    }
    else
    {
      g_free(_data);
      _data = data;
      _length = length;
      tab_changed();
    }
  }
}

void BinaryDataEditor::export_value()
{
  mforms::FileChooser chooser(mforms::SaveFile);
  
  chooser.set_title("Export Field Data");
  if (chooser.run_modal())
  {
    std::string path = chooser.get_path();
    GError *error = 0;
    
    if (!g_file_set_contents(path.c_str(), _data, (gssize)_length, &error))
    {
      mforms::Utilities::show_error(base::strfmt("Could not export data to %s", path.c_str()),
                                    error->message, "OK");
      g_error_free(error);
    }
  }
}

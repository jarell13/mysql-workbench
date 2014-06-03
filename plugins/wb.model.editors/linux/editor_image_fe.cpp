#include "image_cache.h"
#include "linux_utilities/plugin_editor_base.h"
#include "../backend/wb_editor_image.h"
#include <gtkmm/image.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>

class ImageEditorFE : public PluginEditorBase
{
  ImageEditorBE  _be;
  Glib::RefPtr<Gtk::Builder> _xml;
  Gtk::Image    *_image;

  virtual bec::BaseEditor *get_be()
  {
    return &_be;
  }
  
public:
  ImageEditorFE(grt::Module *m, bec::GRTManager *grtm, const grt::BaseListRef &args)
    : PluginEditorBase(m, grtm, args)
    , _be(grtm, workbench_model_ImageFigureRef::cast_from(args[0]))
    , _xml(0)
    , _image(0)
  {
    set_border_width(8);
    
    _xml= Gtk::Builder::create_from_file(grtm->get_data_file_path("modules/data/editor_image.glade"));

    Gtk::Widget *widget;
    _xml->get_widget("editor_image_hbox", widget);

    Gtk::Button *button(0);
    _xml->get_widget("browse_button", button);
    button->signal_clicked().connect(sigc::mem_fun(this, &ImageEditorFE::browse_file));

    _xml->get_widget("reset_size_button", button);
    button->signal_clicked().connect(sigc::mem_fun(this, &ImageEditorFE::reset_aspect));

    Gtk::CheckButton *check;
    _xml->get_widget("aspect_check", check);
    check->signal_toggled().connect(sigc::mem_fun(this, &ImageEditorFE::aspect_toggled));
    
    Gtk::Entry *entry;
    _xml->get_widget("width_entry", entry);
    entry->signal_activate().connect(sigc::mem_fun(this, &ImageEditorFE::width_changed));
    _xml->get_widget("height_entry", entry);
    entry->signal_activate().connect(sigc::mem_fun(this, &ImageEditorFE::height_changed));

    _xml->get_widget("image", _image);
    
    widget->reparent(*this);

    show_all();
    
    refresh_form_data();
  }
  
  void browse_file()
  {
    std::string filename = open_file_chooser();
    if (!filename.empty())
    {
      _be.set_filename(filename);
      do_refresh_form_data();
    }
  }
  

  void aspect_toggled()
  {
    Gtk::CheckButton *check;
    _xml->get_widget("aspect_check", check);
    
    _be.set_keep_aspect_ratio(check->get_active());
  }


  void reset_aspect()
  {
    int w= _image->get_pixbuf()->get_width();
    int h= _image->get_pixbuf()->get_height();

    _be.set_size(w, h);
  }
  
  virtual void do_refresh_form_data()
  {    
    Gtk::Entry *entry;
    int w, h;
    _be.get_size(w, h);
    _xml->get_widget("width_entry", entry);
    entry->set_text(strfmt("%i", w));
    _xml->get_widget("height_entry", entry);
    entry->set_text(strfmt("%i", h));
    
    Gtk::CheckButton *check;
    _xml->get_widget("aspect_check", check);
    check->set_active(_be.get_keep_aspect_ratio());
  
    Glib::RefPtr<Gdk::Pixbuf> pixbuf(Gdk::Pixbuf::create_from_file(_be.get_attached_image_path()));
    if (pixbuf)
      _image->set(pixbuf);  
    else
      g_message("ImageEditorFE: can not set image from %s[%s]", _be.get_filename().c_str(), _be.get_attached_image_path().c_str());    
  }

  void width_changed()
  {
    Gtk::Entry *entry;
    _xml->get_widget("width_entry", entry);
    int i= atoi(entry->get_text().c_str());
    if (i > 0)
      _be.set_width(i);
    do_refresh_form_data();
  }
  
  void height_changed()
  {
    Gtk::Entry *entry;
    _xml->get_widget("height_entry", entry);
    int i= atoi(entry->get_text().c_str());
    if (i > 0)
      _be.set_height(i);
    do_refresh_form_data();
  }
  
};

extern "C" 
{
  GUIPluginBase *createImageEditor(grt::Module *m, bec::GRTManager *grtm, const grt::BaseListRef &args)
  {
    return Gtk::manage(new ImageEditorFE(m, grtm, args));
  }
};
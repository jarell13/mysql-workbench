/* 
 * Copyright (c) 2009, 2013, Oracle and/or its affiliates. All rights reserved.
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


#import "MFSelector.h"

#import "MFView.h"
#import "MFMForms.h"

//--------------------------------------------------------------------------------------------------

@interface PopupStyleSelector : NSPopUpButton {
  mforms::Selector *mOwner;
}

@end

@implementation PopupStyleSelector

- (id) initWithObject: (::mforms::Selector*) aSelector
{
  self= [super initWithFrame:NSMakeRect(0, 0, 10, 20)];
  if (self)
  {    
    mOwner= aSelector;
    mOwner->set_data(self);
    [self setTarget: self];
    [self setAction: @selector(selectionChanged:)];
    [self sizeToFit];
  }
  return self;
}

//--------------------------------------------------------------------------------------------------

- (NSSize) minimumSize
{
  return [[self cell] cellSize];
}

//--------------------------------------------------------------------------------------------------

- (BOOL) heightIsFixed
{
  return YES;
}

//--------------------------------------------------------------------------------------------------

- (void) selectionChanged: (id)sender
{
  mOwner->callback();
}

//--------------------------------------------------------------------------------------------------

- (::mforms::SelectorStyle) selectorStyle
{
  return ::mforms::SelectorPopup;
}

@end

//--------------------------------------------------------------------------------------------------

@interface ComboStyleSelector : NSComboBox <NSComboBoxDelegate>
{
  mforms::Selector *mOwner;
}

@end

@implementation ComboStyleSelector

- (id) initWithObject: (::mforms::Selector*) aSelector
{
  self = [super initWithFrame:NSMakeRect(0, 0, 10, 20)];
  if (self)
  {    
    mOwner= aSelector;
    mOwner->set_data(self);
    [self setTarget: self];
    [self setAction: @selector(selectionChanged:)];
    [self setDelegate: self];
    [self sizeToFit];
  }
  return self;
}

//--------------------------------------------------------------------------------------------------

- (NSSize) minimumSize
{
  return [[self cell] cellSize];
}

//--------------------------------------------------------------------------------------------------

- (BOOL) heightIsFixed
{
  return YES;
}

//--------------------------------------------------------------------------------------------------

- (void) controlTextDidChange:(NSNotification *)obj
{
  mOwner->callback();
}

//--------------------------------------------------------------------------------------------------

- (void) selectionChanged: (id)sender
{
  mOwner->callback();
}

//--------------------------------------------------------------------------------------------------

- (::mforms::SelectorStyle) selectorStyle
{
  return ::mforms::SelectorCombobox;
}

@end

//--------------------------------------------------------------------------------------------------

@implementation MFSelectorImpl

static bool selector_create(::mforms::Selector *self, ::mforms::SelectorStyle style)
{
  switch (style)
  {
    case ::mforms::SelectorPopup:
      [[[PopupStyleSelector alloc] initWithObject:self] autorelease];
      break;
    default:
      [[[ComboStyleSelector alloc] initWithObject:self] autorelease];
      break;
  }
  
  return true;  
}

//--------------------------------------------------------------------------------------------------

static void selector_clear(::mforms::Selector *self)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    [selector removeAllItems];
  }
}

//--------------------------------------------------------------------------------------------------

static int selector_add_item(::mforms::Selector *self, const std::string &item)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    if (item == "-")
      [[selector menu] addItem: [NSMenuItem separatorItem]];
    else
    {
      switch ([selector selectorStyle])
      {
        case ::mforms::SelectorPopup:
          [selector addItemWithTitle: wrap_nsstring(item)];
          break;
        default:
          [selector addItemWithObjectValue: wrap_nsstring(item)];
          break;
      }
    }
    return [selector numberOfItems] - 1;
  }
  return -1;
}

//--------------------------------------------------------------------------------------------------

static void selector_add_items(::mforms::Selector *self, const std::list<std::string> &items)
{
  if (self != NULL)
  {
    for (std::list<std::string>::const_iterator iter= items.begin(); iter != items.end(); ++iter)
      selector_add_item(self, *iter);
  }
}

//--------------------------------------------------------------------------------------------------

static std::string selector_get_item(::mforms::Selector *self, int index)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    switch ([selector selectorStyle])
    {
      case ::mforms::SelectorPopup:
      {
        id value = [selector itemAtIndex: index];
        if (value != nil)
          return [[value title] UTF8String];
        break;
      }
      default:
      {
        id value = [selector itemObjectValueAtIndex: index];
        if (value != nil)
          return [value UTF8String];
        break;
      }
    }
  }
  return "";
}

//--------------------------------------------------------------------------------------------------

static std::string selector_get_text(::mforms::Selector *self)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    id item;
    switch ([selector selectorStyle])
    {
      case ::mforms::SelectorPopup:
        item = [selector titleOfSelectedItem];
        break;
      default:
        item = [selector stringValue];
        break;
    }
    return item ? [item UTF8String] : "";
  }
  return "";
}

//--------------------------------------------------------------------------------------------------

static void selector_set_index(::mforms::Selector *self, int index)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    [selector selectItemAtIndex: index];
    // programmatically setting values should not trigger callbacks
    // ml: questionable
    //self->callback();
  }
}

//--------------------------------------------------------------------------------------------------

static int selector_get_index(::mforms::Selector *self)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    return [selector indexOfSelectedItem];
  }
  return -1;
}

//--------------------------------------------------------------------------------------------------

static int selector_get_item_count(::mforms::Selector *self)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    return [selector numberOfItems];
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------

static void selector_set_value(::mforms::Selector *self, const std::string &value)
{
  if (self != NULL)
  {
    id selector = self->get_data();
    [selector setStringValue: wrap_nsstring(value)];
  }
}

//--------------------------------------------------------------------------------------------------

void cf_selector_init()
{
  ::mforms::ControlFactory *f = ::mforms::ControlFactory::get_instance();
  
  f->_selector_impl.create= &selector_create;
  f->_selector_impl.clear= &selector_clear;
  f->_selector_impl.add_items= &selector_add_items;
  f->_selector_impl.add_item= &selector_add_item;
  f->_selector_impl.get_item= &selector_get_item;
  f->_selector_impl.get_text= &selector_get_text;
  f->_selector_impl.set_index= &selector_set_index;
  f->_selector_impl.get_index= &selector_get_index;
  f->_selector_impl.get_item_count= &selector_get_item_count;
  f->_selector_impl.set_value= &selector_set_value;
}


@end

//--------------------------------------------------------------------------------------------------

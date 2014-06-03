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

#import "MFListBox.h"

#import "MFView.h"
#import "MFMForms.h"

@interface DraggingTableView : NSTableView
{
  mforms::ListBox *mOwner;
  NSTrackingArea *mTrackingArea;
}

@end

@implementation DraggingTableView

- (id)initWithFrame: (NSRect)frame owner: (mforms::ListBox*)theOwner
{
  self = [super initWithFrame: frame];
  if (self != nil)
  {
    mOwner = theOwner;
  }
  return self;
}

//--------------------------------------------------------------------------------------------------

STANDARD_MOUSE_HANDLING(self) // Add handling for mouse events.

//--------------------------------------------------------------------------------------------------

@end

@implementation MFListBoxImpl

- (id)initWithObject:(::mforms::ListBox*)aListBox
{
  self= [super initWithFrame:NSMakeRect(0, 0, 40, 40)];
  if (self)
  { 
    mOwner= aListBox;
    mOwner->set_data(self);
    
    [self setHasVerticalScroller:YES];
    [self setAutohidesScrollers:YES];

    NSRect rect;
    rect.origin= NSMakePoint(0, 0);
    rect.size= [NSScrollView contentSizeForFrameSize: [self frame].size
                               hasHorizontalScroller: NO
                                 hasVerticalScroller: YES
                                          borderType: NSBezelBorder];
    mTable = [[[DraggingTableView alloc] initWithFrame: rect owner: aListBox] autorelease];
    mHeader= [[mTable headerView] retain];
    [mTable setHeaderView: nil];
    [mTable setAllowsMultipleSelection: YES];
    [self setBorderType: NSBezelBorder];
    [self setDocumentView: mTable];
    
    mContents= [[NSMutableArray array] retain];
    [mTable setDataSource: self];
    [mTable setDelegate: self];
    [mTable setColumnAutoresizingStyle: NSTableViewLastColumnOnlyAutoresizingStyle];
    {
      NSTableColumn *column= [[[NSTableColumn alloc] initWithIdentifier: @"0"] autorelease];
    
      [mTable addTableColumn: column];
      [column setResizingMask: NSTableColumnAutoresizingMask|NSTableColumnUserResizingMask];
      //[column setResizable: YES];
      [[column dataCell] setEditable:NO];
    }
  }
  return self;
}


- (void) dealloc
{
  [mHeader release];
  [mContents release];
  [super dealloc];
}


- (void)setEnabled:(BOOL)flag
{
  [mTable setEnabled: flag];
}


- (NSSize)minimumSize
{
  return NSMakeSize(40, 50);
}


- (NSInteger)numberOfRowsInTableView:(NSTableView *)aTableView
{
  return [mContents count];
}


- (id)tableView:(NSTableView *)aTableView objectValueForTableColumn:(NSTableColumn *)aTableColumn row:(NSInteger)rowIndex
{
  return [mContents objectAtIndex: rowIndex];
}


- (void)tableViewSelectionDidChange:(NSNotification *)aNotification
{
  mOwner->selection_changed();
}

// TODO: due to the mix of own tag definition and predefined tag values it is not clear if this
//       is meant to really manipulate the table's tag value or addresses the now named viewFlags property.
- (void)setViewFlags: (NSInteger)tag
{
  mTable.viewFlags = tag;
}

- (NSInteger)viewFlags
{
  return mTable.viewFlags;
}


- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
  [super resizeSubviewsWithOldSize: oldSize];
  [[mTable tableColumnWithIdentifier: @"0"] setWidth: NSWidth([mTable frame])];
}



static bool listbox_create(::mforms::ListBox *self, bool multi_select)
{
  [[[MFListBoxImpl alloc] initWithObject:self] autorelease];
  
  return true;  
}


static void listbox_clear(::mforms::ListBox *self)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      [listbox->mContents removeAllObjects];
      [listbox->mTable reloadData];
    }
  }
}

static void listbox_add_items(::mforms::ListBox *self, const std::list<std::string> &items)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      for (std::list<std::string>::const_iterator iter= items.begin(); iter != items.end(); ++iter)
      {
        [listbox->mContents addObject:wrap_nsstring(*iter)];
      }
      [listbox->mTable reloadData];
    }
  }
}


static size_t listbox_add_item(::mforms::ListBox *self, const std::string &item)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      [listbox->mContents addObject:wrap_nsstring(item)];
      [listbox->mTable reloadData];
      return [listbox->mContents count]-1;
    }
  }
  return -1;
}


static void listbox_remove_indices(mforms::ListBox *self, const std::vector<size_t> &indices)
{
  if (self != nil)
  {
    MFListBoxImpl *listbox = self->get_data();

    if (listbox != nil)
    {
      for (std::vector<size_t>::const_reverse_iterator iterator = indices.rbegin(); iterator != indices.rend();
           ++iterator)
        [listbox->mContents removeObjectAtIndex: *iterator];
      [listbox->mTable reloadData];
    }
  }
}


static void listbox_remove_index(::mforms::ListBox *self, size_t index)
{
  if (self != nil)
  {
    MFListBoxImpl *listbox = self->get_data();

    if (listbox != nil)
    {
      [listbox->mContents removeObjectAtIndex: index];
      [listbox->mTable reloadData];
    }
  }
}


static std::string listbox_get_text(::mforms::ListBox *self)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      if ([listbox->mTable selectedRow] >= 0)
        return [[listbox->mContents objectAtIndex: [listbox->mTable selectedRow]] UTF8String];
    }
  }
  return "";
}


static void listbox_set_index(::mforms::ListBox *self, ssize_t index)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      if (index < 0)
        [listbox->mTable deselectAll: nil];
      else
        [listbox->mTable selectRowIndexes: [NSIndexSet indexSetWithIndex: index] byExtendingSelection:NO];
    }
  }
}


static ssize_t listbox_get_index(::mforms::ListBox *self)
{
  if ( self )
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if ( listbox )
    {
      return [listbox->mTable selectedRow];
    }
  }
  return -1;
}


static std::vector<size_t> listbox_get_selected_indices(::mforms::ListBox *self)
{
  std::vector<size_t> result;
  
  if (self)
  {
    MFListBoxImpl* listbox = self->get_data();
    
    if (listbox)
    {
      NSIndexSet* indices = [listbox->mTable selectedRowIndexes];
      NSUInteger index= [indices firstIndex];
      while (index != NSNotFound)
      {
        result.push_back(index);
        index = [indices indexGreaterThanIndex: index];
      }
    }
  }
  return result;
}


static void listbox_set_heading(::mforms::ListBox *self, const std::string &text)
{
  if (self)
  {
    MFListBoxImpl *listbox= self->get_data();
    
    if (listbox->mHeader)
    {
      [listbox->mTable setHeaderView:listbox->mHeader];
      [listbox->mHeader release];
      listbox->mHeader= nil;
    }
    [[[[listbox->mTable tableColumns] lastObject] headerCell] setStringValue: wrap_nsstring(text)];
  }
}



void cf_listbox_init()
{
  ::mforms::ControlFactory *f = ::mforms::ControlFactory::get_instance();
  
  f->_listbox_impl.create               = &listbox_create;
  f->_listbox_impl.clear                = &listbox_clear;
  f->_listbox_impl.set_heading          = &listbox_set_heading;
  f->_listbox_impl.add_items            = &listbox_add_items;
  f->_listbox_impl.add_item             = &listbox_add_item;
  f->_listbox_impl.remove_indexes       = &listbox_remove_indices;
  f->_listbox_impl.remove_index         = &listbox_remove_index;
  f->_listbox_impl.get_text             = &listbox_get_text;
  f->_listbox_impl.set_index            = &listbox_set_index;
  f->_listbox_impl.get_index            = &listbox_get_index;
  f->_listbox_impl.get_selected_indices = &listbox_get_selected_indices;
}


@end

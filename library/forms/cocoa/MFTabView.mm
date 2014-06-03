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

#import "MFTabView.h"
#import "MFMForms.h"

// XXX: move to mforms
@implementation MFTabViewItemView

- (NSView*)superview
{
  return mTabView;
}


- (void)resizeSubviewsWithOldSize:(NSSize)oldBoundsSize
{
  NSRect frame= NSMakeRect(0, 0, 0, 0);
  
  frame.size= [self frame].size;
  
  if (NSEqualRects(frame, [[[self subviews] lastObject] frame]))
    [[[self subviews] lastObject] resizeSubviewsWithOldSize: oldBoundsSize];
  else
    [[[self subviews] lastObject] setFrame: frame];
}


- (void)setEnabled:(BOOL)flag
{
  [[[self subviews] lastObject] setEnabled: flag];
}

- (NSSize)minimumSize
{
  // A tabview item usually has only one subview attached (the content view), so use this
  // to determine the minimum size.
  if ([[self subviews] count] == 0)
    return NSMakeSize(0, 0);
  return [[[self subviews] objectAtIndex: 0] minimumSize];
}

@end

@interface DraggingTabView : NSTabView
{
  mforms::TabView *mOwner;
  NSTrackingArea *mTrackingArea;
}

@end

@implementation DraggingTabView

- (id)initWithFrame: (NSRect)frame owner: (mforms::TabView *)aTabView
{
  self = [super initWithFrame: frame];
  if (self != nil)
  {
    mOwner = aTabView;
  }
  return self;
}

//--------------------------------------------------------------------------------------------------

STANDARD_MOUSE_HANDLING(self) // Add standard mouse handling.

//--------------------------------------------------------------------------------------------------

@end

@implementation MFTabViewImpl

- (id)initWithObject:(::mforms::TabView*)aTabView tabType:(mforms::TabViewType)tabType
{
  self = [super initWithFrame:NSMakeRect(10, 10, 100, 100)];
  if (self)
  {     
    mTabView = [[[DraggingTabView alloc] initWithFrame: NSMakeRect(0, 0, 100, 100) owner: aTabView] autorelease];

    switch (tabType)
    {
      case mforms::TabViewTabless:
        [mTabView setTabViewType: NSNoTabsNoBorder];
        break;
      case mforms::TabViewMainClosable:
        //[self doCustomize];
        break;
      case mforms::TabViewPalette:
        [mTabView setControlSize: NSSmallControlSize];
        [mTabView setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
        break;
        
      case mforms::TabViewSelectorSecondary:
        [mTabView setTabViewType: NSNoTabsNoBorder];
        mTabSwitcher = [[MTabSwitcher alloc] initWithFrame: NSMakeRect(0, 0, 100, 26)];
        [mTabSwitcher setTabStyle: MPaletteTabSwitcherSmallText];
        [self addSubview: mTabSwitcher];
        [mTabSwitcher setTabView: mTabView];
        break;
      default:
        break;
    }
    [mTabView setDrawsBackground: NO];
    [self addSubview: mTabView];

    mExtraSize = [mTabView minimumSize];
    {
      NSRect contentRect = [mTabView contentRect];
      mExtraSize.width -= NSWidth(contentRect);
      mExtraSize.height -= NSHeight(contentRect);
    }
    mOwner= aTabView;
    mOwner->set_data(self);
    if (mTabSwitcher)
      [mTabSwitcher setDelegate: self];
    else
      [mTabView setDelegate:self];
  }
  return self;
}

- (mforms::Object*)mformsObject
{
  return mOwner;
}

- (NSTabView*)tabView
{
  return mTabView;
}

- (NSSize)minimumSize
{
  if (mOwner == NULL || mOwner->is_destroying())
    return NSZeroSize;
  
  NSSize minSize= NSZeroSize;
  
  for (NSTabViewItem *item in [mTabView tabViewItems])
  {
    NSSize size= [[item view] minimumSize];
    
    minSize.width= MAX(minSize.width, size.width);
    minSize.height= MAX(minSize.height, size.height);
  }
  
  minSize.width += mExtraSize.width;
  minSize.height += mExtraSize.height;
    
  return minSize;
}

// necessary or rebuilding the UI won't work (test case: connection editor)
- (void)resizeSubviewsWithOldSize:(NSSize)oldBoundsSize
{
  [super resizeSubviewsWithOldSize: oldBoundsSize];
  if (mTabSwitcher)
  {
    NSRect srect = [mTabSwitcher frame];
    NSRect rect = [self bounds];

    srect.size.width = NSWidth(rect);
    srect.origin.y = NSHeight(rect) - NSHeight(srect);
    [mTabSwitcher setFrame: srect];

    rect.size.height -= NSHeight(srect);
    [mTabView setFrame: rect];
  }
  else
    [mTabView setFrame: [self bounds]];
  for (NSTabViewItem *item in [mTabView tabViewItems])
    [[item view] resizeSubviewsWithOldSize: oldBoundsSize];
}

- (void)setEnabled:(BOOL)flag
{
  for (NSTabViewItem *item in [mTabView tabViewItems])
    [[item view] setEnabled: flag];
}

- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)tabViewItem
{
  if (!mOwner->is_destroying())
    (*mOwner->signal_tab_changed())();
}


- (void)addTabViewItem: (NSTabViewItem*)item
{
  [mTabView addTabViewItem: item];
}


static bool tabview_create(::mforms::TabView *self, ::mforms::TabViewType tabType)
{
  [[[MFTabViewImpl alloc] initWithObject:self tabType:tabType] autorelease];
    
  return true;  
}


static void tabview_set_active_tab(::mforms::TabView *self, int tab)
{
  if ( self )
  {
    MFTabViewImpl* tabView = self->get_data();
    
    if ( tabView )
    {
      [tabView->mTabView selectTabViewItem: [tabView->mTabView tabViewItemAtIndex:tab]];
    }
  }
}


static void tabview_set_tab_title(::mforms::TabView *self, int tab, const std::string &title)
{
  if ( self )
  {
    MFTabViewImpl* tabView = self->get_data();
    
    if ( tabView )
    {
      [[tabView->mTabView tabViewItemAtIndex:tab] setLabel: wrap_nsstring(title)];
    }
  }
}


static int tabview_get_active_tab(::mforms::TabView *self)
{
  if ( self )
  {
    MFTabViewImpl* tabView = self->get_data();
    
    if ( tabView )
    {
      return [tabView->mTabView indexOfTabViewItem: [tabView->mTabView selectedTabViewItem]];
    }
  }
  return 0;
}


static int tabview_add_page(::mforms::TabView *self, mforms::View *tab, const std::string &label)
{
  if ( self )
  {
    MFTabViewImpl* tabView = self->get_data();
    
    if ( tabView )
    {
      NSTabViewItem *item= [[[NSTabViewItem alloc] initWithIdentifier: [NSString stringWithFormat:@"%p", tab]] autorelease];
      MFTabViewItemView *view= [[MFTabViewItemView alloc] init];
      
      view->mTabView= tabView->mTabView;
      
      [item setLabel: wrap_nsstring(label)];
      [item setView: view];
      
      [view addSubview: tab->get_data()];
      
      [tabView->mTabView addTabViewItem: item];
      
      return [tabView->mTabView numberOfTabViewItems]-1;
    }
  }
  return -1;
}


static void tabview_remove_page(::mforms::TabView *self, mforms::View *tab)
{
  if ( self )
  {
    MFTabViewImpl* tabView = self->get_data();
    
    if (tabView)
    {
      NSInteger i= [tabView->mTabView indexOfTabViewItemWithIdentifier: [NSString stringWithFormat:@"%p", tab]];
      if (i != NSNotFound)
      {
        NSTabViewItem *item= [tabView->mTabView tabViewItemAtIndex: i];
        if (item)
        {
          MFTabViewItemView *view= [item view];
          [[[view subviews] lastObject] removeFromSuperview];
          [view release];
          
          [tabView->mTabView removeTabViewItem: item];
        }      
      }
    }
    else
      NSLog(@"Attempt to remove invalid mforms tabview page");
  }
}


void cf_tabview_init()
{
  ::mforms::ControlFactory *f = ::mforms::ControlFactory::get_instance();
  
  f->_tabview_impl.create= &tabview_create;
  f->_tabview_impl.set_active_tab= &tabview_set_active_tab;
  f->_tabview_impl.get_active_tab= &tabview_get_active_tab;
  f->_tabview_impl.set_tab_title= &tabview_set_tab_title;
  f->_tabview_impl.add_page= &tabview_add_page;
  f->_tabview_impl.remove_page= &tabview_remove_page;
}


@end


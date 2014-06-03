/* 
 * Copyright (c) 2009, 2012, Oracle and/or its affiliates. All rights reserved.
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

#include "base/geometry.h"
#include "base/string_utilities.h"

#import "UserEditor.h"

#import "MCPPUtilities.h"
#import "GRTTreeDataSource.h"

@implementation DbMysqlUserEditor

static void call_refresh(DbMysqlUserEditor *self)
{
  [self performSelectorOnMainThread:@selector(refresh) withObject:nil waitUntilDone:YES];
}


- (id)initWithModule:(grt::Module*)module GRTManager:(bec::GRTManager*)grtm arguments:(const grt::BaseListRef&)args
{
  self= [super initWithNibName: @"UserEditor" bundle: [NSBundle bundleForClass:[self class]]];
  if (self != nil)
  {
    _grtm = grtm;
    // load GUI. Top level view in the nib is the NSTabView that will be docked to the main window
    [self loadView];
    
    // take the minimum size of the view from the initial size in the nib.
    // Therefore the nib should be designed as small as possible
    // note: the honouring of the min size is not yet implemented
    [self setMinimumSize: [tabView frame].size];
  
    [self reinitWithArguments: args];
  }
  return self;
}

- (void)reinitWithArguments:(const grt::BaseListRef&)args
{
  [super reinitWithArguments: args];
  
  bec::GRTManager* grtm = [self grtManager];
  
  delete mBackEnd;
  mBackEnd= new bec::UserEditorBE(grtm, db_UserRef::cast_from(args[0]));
    
  mBackEnd->set_refresh_ui_slot(boost::bind(call_refresh, self));
    
  [mAssignedRoles release];
  mAssignedRoles= [[NSMutableArray array] retain];
  
  [roleTreeDS setTreeModel: mBackEnd->get_role_tree()];
  
  // update the UI
  [self refresh];
}


- (void) dealloc
{
  [mAssignedRoles release];
  delete mBackEnd;
  [super dealloc];
}


/** Fetches object info from the backend and update the UI
 */
- (void)refresh
{
  if (mBackEnd)
  {
    [nameText setStringValue: [NSString stringWithCPPString: mBackEnd->get_name()]];
    [self updateTitle: [self title]];
    
    [passwordText setStringValue: [NSString stringWithCPPString: mBackEnd->get_password()]];
    
    [commentText setString: [NSString stringWithCPPString:mBackEnd->get_comment()]];
    
    [roleOutline reloadData];
    
    [mAssignedRoles removeAllObjects];
    std::vector<std::string> roles(mBackEnd->get_roles());
    for (std::vector<std::string>::const_iterator role= roles.begin(); role != roles.end(); ++role)
    {
      [mAssignedRoles addObject: [NSString stringWithCPPString: *role]];
    }
    [assignedRoleTable reloadData];
    
    [addButton setEnabled: [roleOutline selectedRow] >= 0];
    [removeButton setEnabled: [assignedRoleTable selectedRow] >= 0];
  }
}


- (id)identifier
{
  // an identifier for this editor (just take the object id)
  return [NSString stringWithCPPString:mBackEnd->get_object().id()];
}


- (BOOL)matchesIdentifierForClosingEditor:(NSString*)identifier
{
  return mBackEnd->should_close_on_delete_of([identifier UTF8String]);
}


- (void)tableViewSelectionDidChange:(NSNotification *)aNotification
{
  [removeButton setEnabled: [assignedRoleTable selectedRow] >= 0];
}


- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
  [addButton setEnabled: [roleOutline selectedRow] >= 0];
}



- (IBAction)addRole:(id)sender
{
  NSInteger selectedRow= [roleOutline selectedRow];
  if (selectedRow >= 0 && [roleOutline itemAtRow: selectedRow])
  {
    bec::NodeId node= [roleTreeDS nodeIdForItem: [roleOutline itemAtRow: selectedRow]];
    std::string name;
    
    [roleTreeDS treeModel]->get_field(node, bec::RoleTreeBE::Name, name);
    mBackEnd->add_role(name);
    
    [self refresh];
  }
}


- (IBAction)removeRole:(id)sender
{
  NSInteger selectedRow= [assignedRoleTable selectedRow];
  if (selectedRow >= 0 && selectedRow < (int)[mAssignedRoles count])
  {
    mBackEnd->remove_role([[mAssignedRoles objectAtIndex: selectedRow] UTF8String]);
    
    [self refresh];
  }
}



- (void)controlTextDidEndEditing:(NSNotification *)aNotification
{
  if ([aNotification object] == nameText)
  {
    // set name of the schema
    mBackEnd->set_name([[nameText stringValue] UTF8String]);
  }
  else if ([aNotification object] == passwordText)
  {
    mBackEnd->set_password([[passwordText stringValue] UTF8String]);
  }
}


- (void)textDidEndEditing:(NSNotification *)aNotification
{
  if ([aNotification object] == commentText)
  {
    // set comment for the schema
    mBackEnd->set_comment([[commentText string] UTF8String]);
  }
}


- (NSInteger)numberOfRowsInTableView:(NSTableView *)aTableView
{
  return [mAssignedRoles count];
}


- (id)tableView:(NSTableView *)aTableView objectValueForTableColumn:(NSTableColumn *)aTableColumn row:(NSInteger)rowIndex
{
  return [mAssignedRoles objectAtIndex: rowIndex];
}

- (bec::BaseEditor*)editorBE
{
  return mBackEnd;
}
@end
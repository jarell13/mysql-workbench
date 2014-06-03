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
 
#include "base/log.h"

#include "wf_base.h"
#include "wf_view.h"
#include "wf_treenodeview.h"
#include "wf_treenode.h"
#include "wf_menubar.h"

using namespace System::Drawing;
using namespace System::Collections::Generic;
using namespace System::Collections::ObjectModel;
using namespace System::Windows::Forms;

using namespace Aga::Controls::Tree;
using namespace Aga::Controls::Tree::NodeControls;

using namespace MySQL;
using namespace MySQL::Controls;
using namespace MySQL::Forms;
using namespace MySQL::Utilities;

DEFAULT_LOG_DOMAIN(DOMAIN_MFORMS_WRAPPER)

//----------------- ColumnComparer -----------------------------------------------------------------

/**
  * The class that does the actual text comparison for sorting.
  */
ref class ColumnComparer : public IComparer<Node^>
{
private:
  int column;
  mforms::TreeColumnType type;
  SortOrder direction;
  System::Collections::CaseInsensitiveComparer ^inner;

public:
  ColumnComparer(int aColumn, SortOrder aDirection, mforms::TreeColumnType aType)
  {
    column = aColumn;
    direction = aDirection;
    inner = gcnew System::Collections::CaseInsensitiveComparer();
    type = aType;
  }

  //------------------------------------------------------------------------------------------------

  virtual int Compare(Node ^n1, Node ^n2)
  {
    MySQL::Forms::TreeViewNode ^node1 = dynamic_cast<MySQL::Forms::TreeViewNode ^>(n1);
    MySQL::Forms::TreeViewNode ^node2 = dynamic_cast<MySQL::Forms::TreeViewNode ^>(n2);

    switch (type)
    {
    case mforms::StringColumnType:
    case mforms::IconStringColumnType:
      // There's an attached string for icons, which we use to compare.
      if (direction == SortOrder::Ascending)
        return inner->Compare(node1->Caption[column], node2->Caption[column]);
      else
        return inner->Compare(node2->Caption[column], node1->Caption[column]);
      break;

    case mforms::IntegerColumnType:
    case mforms::LongIntegerColumnType:
    case mforms::CheckColumnType:
    case mforms::TriCheckColumnType:
      {
        Int64 i1 = 0, i2 = 0;
        Int64::TryParse(node1->Caption[column], i1);
        Int64::TryParse(node2->Caption[column], i2);
        if (direction == SortOrder::Ascending)
          return (int)(i1 - i2);
        else
          return int(i2 - i1);
        break;
      }

    case mforms::NumberWithUnitColumnType:
      {
        Double d1 = mforms::TreeNodeView::parse_string_with_unit(NativeToCppStringRaw(node1->Caption[column]).c_str());
        Double d2 = mforms::TreeNodeView::parse_string_with_unit(NativeToCppStringRaw(node2->Caption[column]).c_str());
        if (direction == SortOrder::Ascending)
          return d1.CompareTo(d2);
        else
          return d2.CompareTo(d1);
        break;
      }

    case mforms::FloatColumnType:
      {
        System::Double d1 = 0, d2 = 0;
        System::Double::TryParse(node1->Caption[column], d1);
        System::Double::TryParse(node2->Caption[column], d2);
        if (direction == SortOrder::Ascending)
          return d1.CompareTo(d2);
        else
          return d2.CompareTo(d1);
        break;
      }

    default:
      return 0;
    }
  }

  //------------------------------------------------------------------------------------------------

};

//----------------- SortableTreeModel --------------------------------------------------------------

/**
  * A pretty standard tree model which allows to sort nodes.
  * Note: the way sorting is handled is completely unoptimized and hence not recommended
  *       for larger node counts.
  */
ref class SortableTreeModel : Aga::Controls::Tree::TreeModel
{
private:
  Collections::Generic::IComparer<Node^>^ _comparer;

public:
  virtual Collections::IEnumerable^ GetChildren(TreePath ^treePath) override
  {
    Node ^node = FindNode(treePath);
    if (node != nullptr)
    {
      if (_comparer != nullptr)
        node->Nodes->Sort(_comparer);
     return node->Nodes;
    }

    return nullptr;
  }

  //------------------------------------------------------------------------------------------------

  /**
   * Workaround for tree manipulations not going via the model (which would trigger the
   * structure change event automatically).
   */
  void SortableTreeModel::Resort()
  {
    OnStructureChanged(gcnew TreePathEventArgs());
  }

  //------------------------------------------------------------------------------------------------

  property Collections::Generic::IComparer<Node^>^ Comparer
  {
    Collections::Generic::IComparer<Node^>^ get()
    {
      return _comparer;
    }

    void set(Collections::Generic::IComparer<Node^>^ value)
    { 
      _comparer = value;
      OnStructureChanged(gcnew TreePathEventArgs());
    }
  }

  //------------------------------------------------------------------------------------------------

};

//----------------- AttributedNodeText -------------------------------------------------------------

ref class AttributedNodeText : public NodeTextBox
{
protected:
  virtual void OnDrawText(DrawEventArgs ^args) override
  {
    __super::OnDrawText(args);

    TreeViewNode ^node = dynamic_cast<TreeViewNode^>(args->Node->Tag);

    mforms::TreeNodeTextAttributes attributes = node->Attributes[ParentColumn->Index];
    if (!attributes.bold && !attributes.italic && !attributes.color.is_valid())
      return;

    FontStyle newStyle = FontStyle::Regular;
    if (attributes.bold)
      newStyle = newStyle | FontStyle::Bold;
    if (attributes.italic)
      newStyle = newStyle | FontStyle::Italic;
    args->Font = gcnew Drawing::Font(args->Font, newStyle);
  }
};

//----------------- TriangleNodeControl ------------------------------------------------------------

ref class TriangleNodeControl : public NodeControl
{
private:
  System::Drawing::Bitmap^ _expanded_icon;
  System::Drawing::Bitmap^ _collapsed_icon;
  System::Drawing::Size _size; 

public:
  TriangleNodeControl()
  {
    _expanded_icon = gcnew Bitmap("images/ui/tree_expanded.png", true);
    _collapsed_icon = gcnew Bitmap("images/ui/tree_collapsed.png", true);
  }

  //------------------------------------------------------------------------------------------------

  virtual Size MeasureSize(TreeNodeAdv^ node, DrawContext context) override
  {
    return _expanded_icon->Size;
  }

  //------------------------------------------------------------------------------------------------

  virtual void Draw(TreeNodeAdv^ node, DrawContext context) override
  {
    if (node->CanExpand)
    {
      System::Drawing::Rectangle r = context.Bounds;
      Image ^img;
      if (node->IsExpanded)
        img = _expanded_icon;
      else
        img = _collapsed_icon;
      int dy = (int) Math::Round((float) (r.Height - img->Height) / 2);
      context.Graphics->DrawImageUnscaled(img, System::Drawing::Point(r.X, r.Y + dy));
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void MouseDown(TreeNodeAdvMouseEventArgs^ args) override
  {
    if (args->Button == MouseButtons::Left)
    {
      args->Handled = true;
      if (args->Node->CanExpand)
        args->Node->IsExpanded = !args->Node->IsExpanded;
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void TriangleNodeControl::MouseDoubleClick(TreeNodeAdvMouseEventArgs^ args) override
  {
    args->Handled = true; // Suppress expand/collapse when double clicking.
  }

  //------------------------------------------------------------------------------------------------

};


//----------------- MformsTree ---------------------------------------------------------------------

ref class MformsTree : TreeViewAdv
{
public:
  SortableTreeModel ^model;

  int currentSortColumn;
  bool canSortColumn;
  bool flatList;
  bool alternateRowColors;
  bool canReorderRows;
  bool canBeDragSource;
  Drawing::Rectangle dragBox;
  DataFormats::Format ^rowDragFormat;
  int freezeCount;
  SortOrder currentSortOrder;
  List<int> columnTypes;
  
  Dictionary<String^, TreeViewNode^> ^tagMap;

  MformsTree()
  {
    model = gcnew SortableTreeModel();
    Model = model;
    currentSortColumn = -1;
    canSortColumn = false;
    freezeCount = 0;
    currentSortOrder = SortOrder::None;
    tagMap = nullptr;
    canBeDragSource = false;
    canReorderRows = false;
    rowDragFormat = nullptr;
    dragBox = Rectangle::Empty;
  }
  
  //------------------------------------------------------------------------------------------------

  ~MformsTree()
  {
  }

  //------------------------------------------------------------------------------------------------

  void UseTagMap()
  {
    tagMap = gcnew Dictionary<String^, TreeViewNode^>();
  }

  //------------------------------------------------------------------------------------------------

  void CleanUp(bool clearNodes)
  {
    HideEditor();

    for (int i = 0; i < model->Root->Nodes->Count; ++i)
      dynamic_cast<MySQL::Forms::TreeViewNode ^>(model->Root->Nodes[i])->DestroyDataRecursive();

    if (clearNodes)
      model->Root->Nodes->Clear();
  }

  //------------------------------------------------------------------------------------------------

  int AddColumn(mforms::TreeColumnType type, String ^name, int initial_width, bool editable)
  {
    BindableControl ^nodeControl;
    NodeIcon^ icon = nullptr;
    switch (type)
    {
    case mforms::CheckColumnType:
      nodeControl = gcnew NodeCheckBox();
      break;

    case mforms::TriCheckColumnType:
      {
        NodeCheckBox ^box = gcnew NodeCheckBox();
        box->ThreeState = true;
        nodeControl = box;
        break;
      }

    case mforms::IntegerColumnType:
    case mforms::LongIntegerColumnType:
    case mforms::NumberWithUnitColumnType:
    case mforms::FloatColumnType:
      {
        NodeTextBox ^box = gcnew NodeTextBox(); // Does an integer column work differently than a normal text column (editor-wise)?
        box->EditEnabled = editable;
        box->TextAlign = HorizontalAlignment::Right;
        box->Trimming = StringTrimming::EllipsisCharacter;

        // This enables rendering with TextRenderer instead of Graphics.DrawString.
        // The latter doesn't draw ellipses.
        box->UseCompatibleTextRendering = true;
        nodeControl = box;
        break;
      }

    case mforms::IconColumnType:
      {
        icon = gcnew NodeIcon();

        NodeTextBox ^box = gcnew AttributedNodeText();
        box->EditEnabled= editable;
        box->Trimming = StringTrimming::EllipsisCharacter;
        nodeControl = box;
        break;
      }

    case mforms::StringLTColumnType:
      {
        NodeTextBox ^box = gcnew AttributedNodeText();
        box->EditEnabled = editable;
        box->Trimming = StringTrimming::EllipsisPath;
        nodeControl = box;
        break;
      }

    default: // mforms::StringColumnType
      {
        NodeTextBox ^box = gcnew AttributedNodeText();
        box->EditEnabled = editable;
        box->Trimming = StringTrimming::EllipsisCharacter;
        nodeControl = box;
      }
    };

    TreeColumn ^column = gcnew TreeColumn(name, (initial_width < 0) ? 50 : initial_width);
    Columns->Add(column);
    columnTypes.Add(type);

    if (!flatList && Columns->Count == 1)
    {
      TriangleNodeControl ^control = gcnew TriangleNodeControl();
      control->ParentColumn = column;
      NodeControls->Add(control);
    }

    if (icon != nullptr)
    {
      icon->VirtualMode = true; // Means: get value for that column via event.
      icon->ValueNeeded += gcnew System::EventHandler<NodeControlValueEventArgs^>(this, &MformsTree::TreeValueNeeded);
      icon->LeftMargin = 3;
      icon->ParentColumn = column;

      NodeControls->Add(icon);
    }

    nodeControl->VirtualMode = true;
    nodeControl->ValueNeeded += gcnew System::EventHandler<NodeControlValueEventArgs^>(this, &MformsTree::TreeValueNeeded);
    nodeControl->ValuePushed += gcnew System::EventHandler<NodeControlValueEventArgs^>(this, &MformsTree::TreeValuePushed);
    nodeControl->LeftMargin = 3;
    nodeControl->ParentColumn = column;
    NodeControls->Add(nodeControl);

    return column->Index;
  }

  //------------------------------------------------------------------------------------------------

  void EndColumns() // TODO: not really clear we need that.
  {
    Model = model; // Trigger refresh.
  }

  //------------------------------------------------------------------------------------------------

  mforms::TreeNodeRef NodeFromTag(String ^tag)
  {
    if (tagMap != nullptr)
    {
      TreeViewNode^ node;
      if (tagMap->TryGetValue(tag, node))
      {
        TreeNodeViewWrapper *wrapper = TreeNodeViewWrapper::GetWrapper<TreeNodeViewWrapper>(this);
        return mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, node));
      }
      return mforms::TreeNodeRef();
    }
    throw std::logic_error("Tree node <-> tag mapping requires tree creation option TreeIndexOnTag");
  }

  //--------------------------------------------------------------------------------------------------

  void UpdateTagMap(TreeViewNode ^node, String ^tag)
  {
    if (tagMap != nullptr)
    {
      if (node == nullptr)
        tagMap->Remove(tag);
      else
        tagMap[tag] = node;
    }
  }

  //--------------------------------------------------------------------------------------------------

  void AllowSorting(bool flag)
  {
    canSortColumn = flag;
    if (currentSortColumn > -1)
      Columns[currentSortColumn]->SortOrder = currentSortOrder;
  }

  //--------------------------------------------------------------------------------------------------

  void UpdateSorting(int column)
  {
    if ((freezeCount == 0) && column == currentSortColumn)
      model->Resort();
  }

  //--------------------------------------------------------------------------------------------------

  void FreezeRefresh(bool flag)
  {
    if (flag)
      ++freezeCount;
    else
      if (freezeCount == 0)
        log_error("TreeNodeView: attempt to thaw an unfrozen tree");
      else
        --freezeCount;

    if (freezeCount == 1)
      ControlUtilities::SuspendDrawing(this);
    else
      if (freezeCount == 0)
      {
        ControlUtilities::ResumeDrawing(this);
        model->Resort();
      }
  }

  //--------------------------------------------------------------------------------------------------

  virtual void OnSelectionChanged() override
  {
    __super::OnSelectionChanged();

    mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
    backend->changed();
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnExpanding(TreeNodeAdv ^node) override
  {
    __super::OnExpanding(node);

     TreeViewNode ^ourNode = dynamic_cast<TreeViewNode ^>(node->Tag);
     if (ourNode != nullptr)
     {
       mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
       TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
       backend->expand_toggle(mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, ourNode)), true);
     }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnCollapsed(TreeNodeAdv ^node) override
  {
    __super::OnCollapsed(node);

   TreeViewNode ^ourNode = dynamic_cast<TreeViewNode ^>(node->Tag);
   if (ourNode != nullptr)
   {
     mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
     TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
     backend->expand_toggle(mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, ourNode)), false);
   }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnNodeMouseDoubleClick(TreeNodeAdvMouseEventArgs ^args) override
  {
    __super::OnNodeMouseDoubleClick(args);

    if (args->Control != nullptr)
    {
      TreeNodeAdv ^treeNode = args->Node;
      TreeViewNode ^ourNode = dynamic_cast<TreeViewNode^>(treeNode->Tag);

      if (ourNode != nullptr)
      {
        mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
        TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
        int row = backend->row_for_node(mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, ourNode)));
        int column= args->Control->ParentColumn->Index;
        backend->node_activated(mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, ourNode)), column);
      }
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnColumnClicked(TreeColumn ^column) override
  {
    __super::OnColumnClicked(column);

    if (canSortColumn)
    {
      // Note: TreeViewAdv^ uses sort indicators wrongly. They are used as if they were arrows pointing
      //       to the greater value. However common usage is to view them as normal triangles with the
      //       tip showing the smallest value and the base side the greatest (Windows explorer and many
      //       other controls use it that way). Hence I switch the order here for the columns
      //       (though not for the comparer).
      if (column->Index != currentSortColumn)
      {
        currentSortColumn = column->Index;

        // Initial sort order is always ascending.
        column->SortOrder = SortOrder::Descending;
        currentSortOrder = SortOrder::Ascending;
      }
      else
      {
        if (column->SortOrder == SortOrder::Ascending)
          column->SortOrder = SortOrder::Descending;
        else
          column->SortOrder = SortOrder::Ascending;

        // Revert the sort order for our comparer, read above why.
        currentSortOrder = (column->SortOrder == SortOrder::Ascending) ? SortOrder::Descending : SortOrder::Ascending;
      }

      model->Comparer = gcnew ColumnComparer(currentSortColumn, currentSortOrder,
        (mforms::TreeColumnType)columnTypes[currentSortColumn]);
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnBeforeNodeDrawing(TreeNodeAdv ^node, DrawContext context) override
  {
    __super::OnBeforeNodeDrawing(node, context);

    // Alternating color only for odd rows.
    if (alternateRowColors && ((node->Row % 2) != 0))
    {
      Graphics ^graphics = context.Graphics;
      System::Drawing::Rectangle bounds = context.Bounds;
      System::Drawing::Color color = System::Drawing::Color::FromArgb(237, 243, 253);
      Brush ^brush = gcnew SolidBrush(color);
      graphics->FillRectangle(brush, bounds);
      delete brush;
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnMouseDown(MouseEventArgs ^args) override
  {
    __super::OnMouseDown(args);

    switch (args->Button)
    {
    case ::MouseButtons::Left:
      if (canBeDragSource)
      {
        System::Drawing::Size dragSize = SystemInformation::DragSize;
        dragBox = Drawing::Rectangle(Point(args->X - (dragSize.Width / 2), args->Y - (dragSize.Height / 2)), dragSize);
      }
      break;

    case ::MouseButtons::Right:
      {
        // Update the associated context menu.
        mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
        if (backend->get_context_menu())
        {
          ToolStrip ^menu = MenuBarWrapper::GetManagedObject<ToolStrip>(backend->get_context_menu());
          if (menu != ContextMenuStrip)
          {
            ContextMenuStrip = (Windows::Forms::ContextMenuStrip^)menu;
            if (Conversions::UseWin8Drawing())
              ContextMenuStrip->Renderer = gcnew Win8MenuStripRenderer();
            else
              ContextMenuStrip->Renderer = gcnew TransparentMenuStripRenderer();
            backend->get_context_menu()->will_show();
          }
        }
        else
          ContextMenuStrip = nullptr;
        break;
      }
    }
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnMouseMove(MouseEventArgs ^args) override
  {
    if (dragBox != Rectangle::Empty && !dragBox.Contains(args->Location))
    {
      dragBox = Rectangle::Empty;

      mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
      mforms::DragDetails details;
      void *data = NULL;
      std::string format;

      if (backend->get_drag_data(details, &data, format))
      {
        details.location = base::Point(args->X, args->Y);
        mforms::DragOperation operation = backend->do_drag_drop(details, data, format);
        backend->drag_finished(operation);
      }
      else
        if (SelectedNodes->Count > 0)
        {
          Windows::Forms::DataObject ^data = gcnew Windows::Forms::DataObject();
          DragDropEffects allowedEffects = DragDropEffects::Copy;
          if (canReorderRows)
          {
            allowedEffects = static_cast<DragDropEffects>(allowedEffects | DragDropEffects::Move);
            data->SetData(rowDragFormat->Name, this);
          }

          // Set all selected nodes as strings too, so we can drag their names to text editors etc.
          String ^text = "";
          for each (TreeNodeAdv ^nodeAdv in SelectedNodes)
          {
            TreeViewNode ^node = (TreeViewNode^)nodeAdv->Tag;

            if (text->Length > 0)
              text += ", ";
            text += node->FullCaption;
          }
          data->SetData(DataFormats::UnicodeText, text);

          DoDragDrop(data, allowedEffects);
        }
    }
    else
      __super::OnMouseMove(args);

  }

  //------------------------------------------------------------------------------------------------

  virtual void OnMouseUp(MouseEventArgs ^args) override
  {
    if (args->Button == ::MouseButtons::Left)
      dragBox = Rectangle::Empty;

    __super::OnMouseUp(args);
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnDragOver(DragEventArgs ^args) override
  {
    if (args->Data->GetDataPresent(rowDragFormat->Name))
    {
      // Only accept row move events from this tree instance.
      if (args->Data->GetData(rowDragFormat->Name) == this)
      {
        args->Effect = DragDropEffects::Move;
        return;
      }
    }

    args->Effect = DragDropEffects::None;
  }

  //------------------------------------------------------------------------------------------------

  virtual void OnDragDrop(DragEventArgs ^args) override
  {
    if (args->Data->GetDataPresent(rowDragFormat->Name))
    {
      if (args->Data->GetData(rowDragFormat->Name) == this)
      {
        // Make a copy of the selection. We are going to modify the tree structure.
        Collections::Generic::List<TreeViewNode^> ^selection = gcnew Collections::Generic::List<TreeViewNode^>();
        for each (TreeNodeAdv ^node in SelectedNodes)
          selection->Add((TreeViewNode^)node->Tag);

        Point p = PointToClient(Point(args->X, args->Y));
        NodeControlInfo ^info = GetNodeControlInfoAt(p);
        if (info->Node == nullptr)
        {
          // No target node means either insert before the first one or after the last.
          for each (TreeViewNode ^node in selection)
          {
            node->Parent->Nodes->Remove(node);

            if (p.Y < ColumnHeaderHeight)
              model->Root->Nodes->Insert(0, node);
            else
              model->Root->Nodes->Add(node);
          }
        }
        else
        {
          NodePosition position = DropPosition.Position;
          TreeViewNode ^targetNode = (TreeViewNode^)info->Node->Tag;

          for each (TreeViewNode ^node in selection)
          {
            if (node != targetNode) // Don't move a node to the place it is currently.
            {
              node->Parent->Nodes->Remove(node);
              int index = targetNode->Index + ((position == NodePosition::After) ?  1 : 0);
              targetNode->Parent->Nodes->Insert(index, node);
            }
          }
        }
      }
    }
  }

  //------------------------------------------------------------------------------------------------

  /**
   * Returns the text for the given node control (passed in as sender, representing the column) and for
   * the given node (passed in the event arguments).
   */
  void TreeValueNeeded(System::Object ^sender, NodeControlValueEventArgs ^args)
  {
    BindableControl ^control = (BindableControl ^)sender;
    TreeNodeAdv ^treeNode = args->Node;
    TreeViewNode ^ourNode = dynamic_cast<TreeViewNode ^>(treeNode->Tag);
  
    // The passed in TreeNodeAdv can be the hidden root node, so better check it.
    if (ourNode != nullptr)
    {
      String ^value = ourNode->Caption[control->ParentColumn->Index];
      if (control->GetType() == NodeCheckBox::typeid)
      {
        if ((value == "Checked") || (value == "1"))
          args->Value = CheckState::Checked;
        else
          if ((value == "Unchecked") || (value == "0"))
            args->Value = CheckState::Unchecked;
          else
            args->Value = CheckState::Indeterminate;
      }
      else
        if (control->GetType() == NodeIcon::typeid)
          args->Value = ourNode->Icon[control->ParentColumn->Index];
        else
          args->Value = value;
    }
  }

  //------------------------------------------------------------------------------------------------

  /**
   * Sets new text for the given node control (passed in as sender, representing the column) and for
   * the given node (passed in the event arguments).
   */
  void TreeValuePushed(System::Object ^sender, NodeControlValueEventArgs ^args)
  {
    BindableControl ^control = (BindableControl ^)sender;
    TreeNodeAdv ^treeNode = args->Node;
    TreeViewNode ^ourNode = dynamic_cast<TreeViewNode ^>(treeNode->Tag);

    // First check if the backend allows editing that value.
    std::string new_value = NativeToCppString(args->Value->ToString());
    if (args->Value->GetType() == Boolean::typeid)
      new_value = (bool)args->Value ? "1" : "0";
    else
      if (args->Value->GetType() == CheckState::typeid)
      {
        CheckState state = (CheckState)args->Value;
        new_value = (state == CheckState::Checked) ? "1" : ((state == CheckState::Unchecked) ? "0" : "-1");
      }

    mforms::TreeNodeView *backend = TreeNodeViewWrapper::GetBackend<mforms::TreeNodeView>(this);
    TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
    if (backend->cell_edited(mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, ourNode)), control->ParentColumn->Index, new_value))
    {
      // Backend says ok, so update the model.
      if (ourNode != nullptr)
        ourNode->Caption[control->ParentColumn->Index] = args->Value->ToString();
    }
  }

  //--------------------------------------------------------------------------------------------------

};

//----------------- TreeNodeViewWrapper ------------------------------------------------------------

TreeNodeViewWrapper::TreeNodeViewWrapper(mforms::TreeNodeView *backend)
  : ViewWrapper(backend)
{
}

//--------------------------------------------------------------------------------------------------

TreeNodeViewWrapper::~TreeNodeViewWrapper()
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>();
  control->CleanUp(false);
};

//--------------------------------------------------------------------------------------------------

bool TreeNodeViewWrapper::create(mforms::TreeNodeView *backend, mforms::TreeOptions options)
{
  TreeNodeViewWrapper *wrapper = new TreeNodeViewWrapper(backend);

  MformsTree ^tree = TreeNodeViewWrapper::Create<MformsTree>(backend, wrapper);
  if ((options & mforms::TreeIndexOnTag) != 0)
    tree->UseTagMap();

  if ((options & mforms::TreeCanBeDragSource) != 0 || (options & mforms::TreeAllowReorderRows) != 0)
  {
    tree->canBeDragSource = true;
    if ((options & mforms::TreeAllowReorderRows) != 0)
    {
      // Register an own format for row reorder.
      tree->rowDragFormat = DataFormats::GetFormat("com.mysql.workbench.row-reorder");
      tree->AllowDrop = true;
      tree->canReorderRows = true;
    }
  }

  tree->FullRowSelect = true; // Same as in Explorer.
  tree->AsyncExpanding = false;
  tree->LoadOnDemand = true;
  tree->UseColumns = options & mforms::TreeNoColumns ? false : true;
  tree->ShowHeader = tree->UseColumns && (options & mforms::TreeNoHeader) == 0;
  
  if (options & mforms::TreeNoBorder) // Default is 3d border.
  {
    tree->BorderStyle = BorderStyle::None;

    // Add some padding or the content will directly start at the boundaries.
    tree->Padding = Padding(2);
  }

  tree->ShowLines = false;
  tree->ShowPlusMinus = false;

  tree->flatList = (options & mforms::TreeFlatList) != 0;
  tree->alternateRowColors = (options & mforms::TreeAltRowColors) != 0;

  return true;
}

//--------------------------------------------------------------------------------------------------

int TreeNodeViewWrapper::add_column(mforms::TreeNodeView *backend, mforms::TreeColumnType type,
  const std::string &name, int initial_width, bool editable)
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>(backend);
  return control->AddColumn(type, CppStringToNative(name), initial_width, editable);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::end_columns(mforms::TreeNodeView *backend)
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>(backend);
  control->EndColumns();
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::clear(mforms::TreeNodeView *backend)
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>(backend);
  control->CleanUp(true);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_row_height(mforms::TreeNodeView *backend, int h)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->set_row_height(h);
}

//--------------------------------------------------------------------------------------------------

std::list<mforms::TreeNodeRef> TreeNodeViewWrapper::get_selection(mforms::TreeNodeView *backend)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->get_selection();
}

//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::get_selected_node(mforms::TreeNodeView *backend)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  TreeViewAdv ^tree = wrapper->GetManagedObject<TreeViewAdv>();
  TreeNodeAdv ^snode = tree->CurrentNode;
  if (snode == nullptr || snode->Tag == nullptr)
    return mforms::TreeNodeRef();
  return mforms::TreeNodeRef(new TreeNodeWrapper(wrapper, (TreeViewNode^)snode->Tag));
}

//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::root_node(mforms::TreeNodeView *backend)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->root_node();
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_selected(mforms::TreeNodeView *backend, mforms::TreeNodeRef node, bool flag)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->set_selected(node, flag);
}


//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::clear_selection(mforms::TreeNodeView *backend)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->clear_selection();
}

//--------------------------------------------------------------------------------------------------

mforms::TreeSelectionMode TreeNodeViewWrapper::get_selection_mode(mforms::TreeNodeView *backend)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->get_selection_mode();
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_selection_mode(mforms::TreeNodeView *backend, mforms::TreeSelectionMode mode)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->set_selection_mode(mode);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_allow_sorting(mforms::TreeNodeView *backend, bool flag)
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>(backend);
  control->AllowSorting(flag);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::freeze_refresh(mforms::TreeNodeView *backend, bool flag)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->freeze_refresh(flag);
}

//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::node_at_row(mforms::TreeNodeView *backend, int row)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->node_at_row(row);
}

//--------------------------------------------------------------------------------------------------

int TreeNodeViewWrapper::row_for_node(mforms::TreeNodeView *backend, mforms::TreeNodeRef node)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->row_for_node(node);
}

//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::node_with_tag(mforms::TreeNodeView *backend, const std::string &tag)
{
  MformsTree ^control = TreeNodeViewWrapper::GetManagedObject<MformsTree>(backend);
  return control->NodeFromTag(CppStringToNative(tag));
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_column_visible(mforms::TreeNodeView *backend, int column, bool flag)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->set_column_visible(column, flag);
}

//--------------------------------------------------------------------------------------------------

bool TreeNodeViewWrapper::get_column_visible(mforms::TreeNodeView *backend, int column)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->is_column_visible(column);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_column_width(mforms::TreeNodeView *backend, int column, int width)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  wrapper->set_column_width(column, width);
}

//--------------------------------------------------------------------------------------------------

int TreeNodeViewWrapper::get_column_width(mforms::TreeNodeView *backend, int column)
{
  TreeNodeViewWrapper *wrapper = backend->get_data<TreeNodeViewWrapper>();
  return wrapper->get_column_width(column);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_row_height(int h)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  tree->RowHeight = h;
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::clear_selection()
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  tree->ClearSelection();
}

//--------------------------------------------------------------------------------------------------

std::list<mforms::TreeNodeRef> TreeNodeViewWrapper::get_selection()
{
  std::list<mforms::TreeNodeRef> selection;
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  ReadOnlyCollection<TreeNodeAdv^> ^sel = tree->SelectedNodes;
  if (sel != nullptr)
  {
    for (int i = 0; i < sel->Count; i++)
    {
      TreeViewNode ^node = (TreeViewNode^)sel[i]->Tag;
      selection.push_back(mforms::TreeNodeRef(new TreeNodeWrapper(this, node)));
    }
  }
  return selection;
}

//--------------------------------------------------------------------------------------------------

mforms::TreeSelectionMode TreeNodeViewWrapper::get_selection_mode()
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  switch (tree->SelectionMode)
  {
  case Aga::Controls::Tree::TreeSelectionMode::Single:  
    return mforms::TreeSelectSingle;

  case Aga::Controls::Tree::TreeSelectionMode::Multi:
    return mforms::TreeSelectMultiple;

  default:
    return mforms::TreeSelectSingle;
  }
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_selection_mode(mforms::TreeSelectionMode mode)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  switch (mode)
  {  
  case mforms::TreeSelectSingle:
    tree->SelectionMode = Aga::Controls::Tree::TreeSelectionMode::Single;
    break;
  case mforms::TreeSelectMultiple:
    tree->SelectionMode = Aga::Controls::Tree::TreeSelectionMode::Multi;
    break;
  }
}
//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::root_node()
{
  return mforms::TreeNodeRef(new TreeNodeWrapper(this));
}

//--------------------------------------------------------------------------------------------------

static mforms::TreeNodeRef find_node_at_row(mforms::TreeNodeRef node, int &row_counter, int row)
{
  mforms::TreeNodeRef res;
  for (int i = 0, c = node->count(); i < c; i++)
  {
    mforms::TreeNodeRef child = node->get_child(i);
    if (row_counter == row)
      return child;
    row_counter++;
    if (child->is_expanded())
    {
      res = find_node_at_row(child, row_counter, row);
      if (res)
        return res;
    }
  }
  return res;
}

//--------------------------------------------------------------------------------------------------

mforms::TreeNodeRef TreeNodeViewWrapper::node_at_row(int row)
{
  int i = 0;
  mforms::TreeNodeRef node = find_node_at_row(root_node(), i, row);
  return node;
}

//--------------------------------------------------------------------------------------------------

static int count_rows_in_node(mforms::TreeNodeRef node)
{
  if (node->is_expanded())
  {
    int count = node->count();
    for (int i = 0, c = node->count(); i < c; i++)
    {
      mforms::TreeNodeRef child(node->get_child(i));
      if (child)
        count += count_rows_in_node(child);
    }
    return count;
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------

int TreeNodeViewWrapper::row_for_node(mforms::TreeNodeRef node)
{
  TreeNodeWrapper *impl = dynamic_cast<TreeNodeWrapper*>(node.ptr());
  if (impl)
  {
    mforms::TreeNodeRef parent = node->get_parent();
    int node_index = impl->node_index();
    int row = node_index;

    if (parent)
    {
      for (int i = 0; i < node_index; i++)
        row += count_rows_in_node(parent->get_child(i));
      if (parent != root_node())
        row += row_for_node(parent);
    }
    return row;
  }
  return -1;
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_column_visible(int column, bool flag)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  tree->Columns[column]->IsVisible = flag;
}

//--------------------------------------------------------------------------------------------------

bool TreeNodeViewWrapper::is_column_visible(int column)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  return tree->Columns[column]->IsVisible;
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_column_width(int column, int width)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  tree->Columns[column]->Width = width;
}

//--------------------------------------------------------------------------------------------------

int TreeNodeViewWrapper::get_column_width(int column)
{
  TreeViewAdv ^tree = GetManagedObject<TreeViewAdv>();
  return tree->Columns[column]->Width;
}

//--------------------------------------------------------------------------------------------------

/**
 * Adds, removes or changes a node <-> tag mapping (if mapping is enabled).
 */
void TreeNodeViewWrapper::process_mapping(TreeViewNode ^node, const std::string &tag)
{
  MformsTree ^tree = GetManagedObject<MformsTree>();
  tree->UpdateTagMap(node, CppStringToNative(tag));
}

//--------------------------------------------------------------------------------------------------

/**
 * Called by a treeview node if new text was set.
 */
void TreeNodeViewWrapper::node_value_set(int column)
{
  MformsTree ^tree = GetManagedObject<MformsTree>();
  tree->UpdateSorting(column);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::set_selected(mforms::TreeNodeRef node, bool flag)
{
  TreeNodeWrapper *impl = dynamic_cast<TreeNodeWrapper*>(node.ptr());
  if (impl)
  {
    TreeNodeAdv ^tna = impl->find_node_adv();
    if (tna)
      tna->IsSelected = true;
  }
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::freeze_refresh(bool flag)
{
  MformsTree ^tree = GetManagedObject<MformsTree>();
  tree->FreezeRefresh(flag);
}

//--------------------------------------------------------------------------------------------------

void TreeNodeViewWrapper::init()
{
  mforms::ControlFactory *f = mforms::ControlFactory::get_instance();

  f->_treenodeview_impl.create = &TreeNodeViewWrapper::create;
  f->_treenodeview_impl.add_column = &TreeNodeViewWrapper::add_column;
  f->_treenodeview_impl.end_columns = &TreeNodeViewWrapper::end_columns;
  f->_treenodeview_impl.clear = &TreeNodeViewWrapper::clear;
  f->_treenodeview_impl.clear_selection = &TreeNodeViewWrapper::clear_selection;
  f->_treenodeview_impl.get_selection = &TreeNodeViewWrapper::get_selection;
  f->_treenodeview_impl.get_selected_node = &TreeNodeViewWrapper::get_selected_node;
  f->_treenodeview_impl.set_selected = &TreeNodeViewWrapper::set_selected;
  f->_treenodeview_impl.set_allow_sorting = &TreeNodeViewWrapper::set_allow_sorting;
  f->_treenodeview_impl.set_row_height = &TreeNodeViewWrapper::set_row_height;
  f->_treenodeview_impl.freeze_refresh = &TreeNodeViewWrapper::freeze_refresh;
  f->_treenodeview_impl.root_node = &TreeNodeViewWrapper::root_node;
  f->_treenodeview_impl.row_for_node = &TreeNodeViewWrapper::row_for_node;
  f->_treenodeview_impl.node_at_row = &TreeNodeViewWrapper::node_at_row;
  f->_treenodeview_impl.set_selection_mode = &TreeNodeViewWrapper::set_selection_mode;
  f->_treenodeview_impl.get_selection_mode = &TreeNodeViewWrapper::get_selection_mode;
  f->_treenodeview_impl.node_with_tag = &TreeNodeViewWrapper::node_with_tag;

  f->_treenodeview_impl.set_column_visible = &TreeNodeViewWrapper::set_column_visible;
  f->_treenodeview_impl.get_column_visible = &TreeNodeViewWrapper::get_column_visible;

  f->_treenodeview_impl.set_column_width = &TreeNodeViewWrapper::set_column_width;
  f->_treenodeview_impl.get_column_width = &TreeNodeViewWrapper::get_column_width;
}

//--------------------------------------------------------------------------------------------------

// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app/menus/simple_menu_model.h"

#include "app/l10n_util.h"

static const int kSeparatorId = -1;

namespace menus {

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, public:

SimpleMenuModel::SimpleMenuModel(Delegate* delegate) : delegate_(delegate) {
}

SimpleMenuModel::~SimpleMenuModel() {
}

void SimpleMenuModel::AddItem(int command_id, const string16& label) {
  Item item = { command_id, label, SkBitmap(), TYPE_COMMAND, -1, NULL, NULL };
  AppendItem(item);
}

void SimpleMenuModel::AddItemWithStringId(int command_id, int string_id) {
  AddItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddSeparator() {
  Item item = { kSeparatorId, string16(), SkBitmap(), TYPE_SEPARATOR, -1,
                NULL, NULL };
  AppendItem(item);
}

void SimpleMenuModel::AddCheckItem(int command_id, const string16& label) {
  Item item = { command_id, label, SkBitmap(), TYPE_CHECK, -1, NULL };
  AppendItem(item);
}

void SimpleMenuModel::AddCheckItemWithStringId(int command_id, int string_id) {
  AddCheckItem(command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::AddRadioItem(int command_id, const string16& label,
                                   int group_id) {
  Item item = { command_id, label, SkBitmap(), TYPE_RADIO, group_id, NULL,
                NULL };
  AppendItem(item);
}

void SimpleMenuModel::AddRadioItemWithStringId(int command_id, int string_id,
                                               int group_id) {
  AddRadioItem(command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::AddButtonItem(int command_id,
                                    ButtonMenuItemModel* model) {
  Item item = { 0, string16(), SkBitmap(), TYPE_BUTTON_ITEM, -1, NULL, model };
  AppendItem(item);
}

void SimpleMenuModel::AddSubMenu(int command_id, const string16& label,
                                 MenuModel* model) {
  Item item = { command_id, label, SkBitmap(), TYPE_SUBMENU, -1, model, NULL };
  AppendItem(item);
}

void SimpleMenuModel::AddSubMenuWithStringId(int command_id,
                                             int string_id, MenuModel* model) {
  AddSubMenu(command_id, l10n_util::GetStringUTF16(string_id), model);
}

void SimpleMenuModel::InsertItemAt(
    int index, int command_id, const string16& label) {
  Item item = { command_id, label, SkBitmap(), TYPE_COMMAND, -1, NULL, NULL };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertItemAt(index, command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertSeparatorAt(int index) {
  Item item = { kSeparatorId, string16(), SkBitmap(), TYPE_SEPARATOR, -1,
                NULL, NULL };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertCheckItemAt(
    int index, int command_id, const string16& label) {
  Item item = { command_id, label, SkBitmap(), TYPE_CHECK, -1, NULL, NULL };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertCheckItemWithStringIdAt(
    int index, int command_id, int string_id) {
  InsertCheckItemAt(
      FlipIndex(index), command_id, l10n_util::GetStringUTF16(string_id));
}

void SimpleMenuModel::InsertRadioItemAt(
    int index, int command_id, const string16& label, int group_id) {
  Item item = { command_id, label, SkBitmap(), TYPE_RADIO, group_id, NULL,
                NULL };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertRadioItemWithStringIdAt(
    int index, int command_id, int string_id, int group_id) {
  InsertRadioItemAt(
      index, command_id, l10n_util::GetStringUTF16(string_id), group_id);
}

void SimpleMenuModel::InsertSubMenuAt(
    int index, int command_id, const string16& label, MenuModel* model) {
  Item item = { command_id, label, SkBitmap(), TYPE_SUBMENU, -1, model, NULL };
  InsertItemAtIndex(item, index);
}

void SimpleMenuModel::InsertSubMenuWithStringIdAt(
    int index, int command_id, int string_id, MenuModel* model) {
  InsertSubMenuAt(index, command_id, l10n_util::GetStringUTF16(string_id),
                  model);
}

void SimpleMenuModel::SetIcon(int index, const SkBitmap& icon) {
  items_[index].icon = icon;
}

int SimpleMenuModel::GetIndexOfCommandId(int command_id) {
  std::vector<Item>::iterator itr;
  for (itr = items_.begin(); itr != items_.end(); itr++) {
    if ((*itr).command_id == command_id) {
      return FlipIndex(static_cast<int>(std::distance(items_.begin(), itr)));
    }
  }
  return -1;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, MenuModel implementation:

bool SimpleMenuModel::HasIcons() const {
  for (std::vector<Item>::const_iterator iter = items_.begin();
       iter != items_.end(); ++iter) {
    if (!iter->icon.isNull())
      return true;
  }

  return false;
}

int SimpleMenuModel::GetItemCount() const {
  return static_cast<int>(items_.size());
}

MenuModel::ItemType SimpleMenuModel::GetTypeAt(int index) const {
  return items_.at(FlipIndex(index)).type;
}

int SimpleMenuModel::GetCommandIdAt(int index) const {
  return items_.at(FlipIndex(index)).command_id;
}

string16 SimpleMenuModel::GetLabelAt(int index) const {
  if (IsLabelDynamicAt(index))
    return delegate_->GetLabelForCommandId(GetCommandIdAt(index));
  return items_.at(FlipIndex(index)).label;
}

bool SimpleMenuModel::IsLabelDynamicAt(int index) const {
  if (delegate_)
    return delegate_->IsLabelForCommandIdDynamic(GetCommandIdAt(index));
  return false;
}

bool SimpleMenuModel::GetAcceleratorAt(int index,
                                       menus::Accelerator* accelerator) const {
  if (delegate_) {
    return delegate_->GetAcceleratorForCommandId(GetCommandIdAt(index),
                                                 accelerator);
  }
  return false;
}

bool SimpleMenuModel::IsItemCheckedAt(int index) const {
  if (!delegate_)
    return false;
  int item_index = FlipIndex(index);
  MenuModel::ItemType item_type = items_[item_index].type;
  return (item_type == TYPE_CHECK || item_type == TYPE_RADIO) ?
      delegate_->IsCommandIdChecked(GetCommandIdAt(index)) : false;
}

int SimpleMenuModel::GetGroupIdAt(int index) const {
  return items_.at(FlipIndex(index)).group_id;
}

bool SimpleMenuModel::GetIconAt(int index, SkBitmap* icon) const {
  if (items_[index].icon.isNull())
    return false;

  *icon = items_[index].icon;
  return true;
}

ButtonMenuItemModel* SimpleMenuModel::GetButtonMenuItemAt(int index) const {
  return items_.at(FlipIndex(index)).button_model;
}

bool SimpleMenuModel::IsEnabledAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (!delegate_ || command_id == kSeparatorId ||
      items_.at(FlipIndex(index)).button_model)
    return true;
  return delegate_->IsCommandIdEnabled(command_id);
}

void SimpleMenuModel::HighlightChangedTo(int index) {
  if (delegate_)
    delegate_->CommandIdHighlighted(GetCommandIdAt(index));
}

void SimpleMenuModel::ActivatedAt(int index) {
  if (delegate_)
    delegate_->ExecuteCommand(GetCommandIdAt(index));
}

MenuModel* SimpleMenuModel::GetSubmenuModelAt(int index) const {
  return items_.at(FlipIndex(index)).submenu;
}

////////////////////////////////////////////////////////////////////////////////
// SimpleMenuModel, Private:

void SimpleMenuModel::AppendItem(const Item& item) {
  ValidateItem(item);
  items_.push_back(item);
}

void SimpleMenuModel::InsertItemAtIndex(const Item& item, int index) {
  ValidateItem(item);
  items_.insert(items_.begin() + FlipIndex(index), item);
}

void SimpleMenuModel::ValidateItem(const Item& item) {
#ifndef NDEBUG
  if (item.type == TYPE_SEPARATOR) {
    DCHECK_EQ(item.command_id, kSeparatorId);
  } else {
    DCHECK_GE(item.command_id, 0);
  }
#endif  // NDEBUG
}

}  // namespace menus

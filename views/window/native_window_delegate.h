// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_WIDGET_NATIVE_WINDOW_DELEGATE_H_
#define VIEWS_WIDGET_NATIVE_WINDOW_DELEGATE_H_
#pragma once

namespace ui {
class ThemeProvider;
}

namespace views {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// NativeWindowDelegate interface
//
//  An interface implemented by an object that receives notifications from a
//  NativeWindow implementation.
//
class NativeWindowDelegate {
 public:
  virtual ~NativeWindowDelegate() {}

  // Returns true if the window is modal.
  virtual bool IsModal() const = 0;

  // Returns true if the window is a dialog box.
  virtual bool IsDialogBox() const = 0;

  // Called just after the NativeWindow has been created.
  virtual void OnNativeWindowCreated(const gfx::Rect& bounds) = 0;

  //
  virtual Window* AsWindow() = 0;

  //
  virtual NativeWidgetDelegate* AsNativeWidgetDelegate() = 0;
};

}  // namespace internal
}  // namespace views

#endif  // VIEWS_WIDGET_NATIVE_WINDOW_DELEGATE_H_

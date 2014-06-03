/* 
 * Copyright (c) 2008, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef _MFORMS_PROGRESSBAR_H_
#define _MFORMS_PROGRESSBAR_H_

#include <mforms/base.h>
#include <mforms/view.h>


namespace mforms {
  class ProgressBar;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#ifndef SWIG
  struct ProgressBarImplPtrs
  {
    bool (*create)(ProgressBar *self);
    void (*set_value)(ProgressBar *self, float pct);
    void (*set_indeterminate)(ProgressBar *self, bool flag);
    void (*set_started)(ProgressBar *self, bool flag);
  };
#endif
#endif

  /** A progress bar to show completion state of a task. */
  class MFORMS_EXPORT ProgressBar : public View
  {
  public:
    ProgressBar();

    /** Sets whether the progressbar knows how much actual progress was made. */
    void set_indeterminate(bool flag);
    
    /** Starts animating the progressbar to indicate the task is in progress. */
    void start();
    /** Stops animating the progressbar. */
    void stop();
    /** Sets the progress value (0.0 to 1.0) */
    void set_value(float pct);

  protected:
    ProgressBarImplPtrs *_progressbar_impl;
  };
};


#endif /* _MFORMS_PROGRESSBAR_H_ */
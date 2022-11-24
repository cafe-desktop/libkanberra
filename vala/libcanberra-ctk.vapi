/***
  This file is part of libcanberra.

  Copyright 2009 Lennart Poettering

  libcanberra is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 2.1 of the
  License, or (at your option) any later version.

  libcanberra is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with libcanberra. If not, see
  <http://www.gnu.org/licenses/>.
***/

using Canberra;
using Cdk;
using Ctk;

[CCode (cprefix = "CA_CTK_", lower_case_cprefix = "ca_ctk_", cheader_filename = "canberra-ctk.h")]
namespace CanberraCtk {

        public unowned Context? context_get();
        public unowned Context? context_get_for_screen(Cdk.Screen? screen);

        public int proplist_set_for_widget(Proplist p, Ctk.Widget w);
        public int play_for_widget(Ctk.Widget w, uint32 id, ...);
        public int proplist_set_for_event(Proplist p, Cdk.Event e);
        public int play_for_event(Cdk.Event e, uint32 id, ...);

        public void widget_disable_sounds(Ctk.Widget w, bool enable = false);
}
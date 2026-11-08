#pragma once
// widgets::searchable -- type to find in a Gtk::DropDown over a StringList
// (s014: the Trails tab's metric pickers and the trigger editor, ~70 metrics
// on the M1). gtkmm doesn't wrap GtkPropertyExpression, so the C API.
// Substring matching where GTK has it (4.12+), prefix before that.
#include <gtk/gtk.h>
#include <gtkmm/dropdown.h>

namespace trails::widgets {

inline void searchable(Gtk::DropDown& dd) {
    GtkExpression* e = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string");
    gtk_drop_down_set_expression(dd.gobj(), e);   // takes its own reference
    gtk_expression_unref(e);
    gtk_drop_down_set_enable_search(dd.gobj(), TRUE);
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_drop_down_set_search_match_mode(dd.gobj(), GTK_STRING_FILTER_MATCH_MODE_SUBSTRING);
#endif
}

}  // namespace trails::widgets

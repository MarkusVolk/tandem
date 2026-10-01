/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui.h"

typedef struct
{
  TandemGuiDevice *device;
  gint64 thread_id;
  GStrv addresses;
  GtkWidget *list;
  GtkWidget *scrolled;
  GtkWidget *entry;
  GtkWidget *older_button;
  gulong changed_id;
  gboolean scroll_to_end;
  gint64 first_date;
} ConversationPage;

static char *
format_date (gint64 ms)
{
  g_autoptr(GDateTime) when = g_date_time_new_from_unix_local (ms / 1000);
  g_autoptr(GDateTime) now = g_date_time_new_now_local ();

  if (when == NULL)
    return g_strdup ("");
  if (g_date_time_get_year (when) == g_date_time_get_year (now) &&
      g_date_time_get_day_of_year (when) == g_date_time_get_day_of_year (now))
    return g_date_time_format (when, "%H:%M");
  return g_date_time_format (when, "%d.%m.%Y %H:%M");
}

static void
attachment_clicked_cb (GtkButton *button, gpointer user_data)
{
  ConversationPage *page = user_data;
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  g_variant_builder_add (args, "{sv}", "part_id", g_variant_new_int64 (GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (button), "part"))));
  g_variant_builder_add (args, "{sv}", "unique_identifier", g_variant_new_string (g_object_get_data (G_OBJECT (button), "unique")));
  tandem_gui_device_call_plugin (page->device, "sms", "attachment", g_variant_builder_end (args));
}

static GtkWidget *
message_row_new (ConversationPage *page, GVariant *message)
{
  const char *body = "";
  gint64 date = 0;
  gboolean outgoing = FALSE;
  g_autoptr(GVariant) attachments = NULL;
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *bubble = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *label;
  g_autofree char *date_text = NULL;
  GtkWidget *date_label;

  g_variant_lookup (message, "body", "&s", &body);
  g_variant_lookup (message, "date", "x", &date);
  g_variant_lookup (message, "outgoing", "b", &outgoing);
  attachments = g_variant_lookup_value (message, "attachments", G_VARIANT_TYPE ("aa{sv}"));

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_add_css_class (bubble, "card");
  gtk_widget_set_halign (bubble, outgoing ? GTK_ALIGN_END : GTK_ALIGN_START);
  gtk_widget_set_margin_top (bubble, 4);
  gtk_widget_set_margin_bottom (bubble, 4);
  gtk_widget_set_margin_start (bubble, outgoing ? 60 : 8);
  gtk_widget_set_margin_end (bubble, outgoing ? 8 : 60);
  if (*body != '\0')
    {
      label = gtk_label_new (body);
      gtk_label_set_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
      gtk_label_set_xalign (GTK_LABEL (label), 0);
      gtk_label_set_selectable (GTK_LABEL (label), TRUE);
      gtk_widget_set_margin_start (label, 10);
      gtk_widget_set_margin_end (label, 10);
      gtk_widget_set_margin_top (label, 8);
      gtk_box_append (GTK_BOX (bubble), label);
    }

  if (attachments != NULL)
    {
      GVariantIter iter;
      GVariant *attachment;

      g_variant_iter_init (&iter, attachments);
      while ((attachment = g_variant_iter_next_value (&iter)) != NULL)
        {
          const char *mime = "", *unique = "";
          gint64 part_id = 0;
          g_autoptr(GVariant) thumb = g_variant_lookup_value (attachment, "thumbnail", G_VARIANT_TYPE_BYTESTRING);
          GtkWidget *button = gtk_button_new ();
          GtkWidget *child = NULL;

          g_variant_lookup (attachment, "mime_type", "&s", &mime);
          g_variant_lookup (attachment, "unique_identifier", "&s", &unique);
          g_variant_lookup (attachment, "part_id", "x", &part_id);
          if (thumb != NULL)
            {
              g_autoptr(GBytes) bytes = g_variant_get_data_as_bytes (thumb);
              g_autoptr(GdkTexture) texture = gdk_texture_new_from_bytes (bytes, NULL);

              if (texture != NULL)
                {
                  child = gtk_picture_new_for_paintable (GDK_PAINTABLE (texture));
                  gtk_picture_set_can_shrink (GTK_PICTURE (child), TRUE);
                  gtk_widget_set_size_request (child, 160, 120);
                }
            }
          if (child == NULL)
            child = gtk_label_new (mime);
          gtk_button_set_child (GTK_BUTTON (button), child);
          gtk_widget_add_css_class (button, "flat");
          gtk_widget_set_margin_start (button, 6);
          gtk_widget_set_margin_end (button, 6);
          g_object_set_data (G_OBJECT (button), "part", GSIZE_TO_POINTER (part_id));
          g_object_set_data_full (G_OBJECT (button), "unique", g_strdup (unique), g_free);
          g_signal_connect (button, "clicked", G_CALLBACK (attachment_clicked_cb), page);
          gtk_box_append (GTK_BOX (bubble), button);
          g_variant_unref (attachment);
        }
    }

  date_text = format_date (date);
  date_label = gtk_label_new (date_text);
  gtk_widget_add_css_class (date_label, "dim-label");
  gtk_widget_add_css_class (date_label, "caption");
  gtk_label_set_xalign (GTK_LABEL (date_label), outgoing ? 1 : 0);
  gtk_widget_set_margin_start (date_label, 10);
  gtk_widget_set_margin_end (date_label, 10);
  gtk_widget_set_margin_bottom (date_label, 6);
  gtk_box_append (GTK_BOX (bubble), date_label);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), bubble);
  return row;
}

static gboolean
scroll_to_end_cb (gpointer user_data)
{
  ConversationPage *page = user_data;
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (page->scrolled));

  gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
  return G_SOURCE_REMOVE;
}

static void
rebuild_messages (ConversationPage *page)
{
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GVariant) messages = NULL;
  GVariantIter iter;
  GVariant *message;
  GtkWidget *child;
  guint n = 0;

  g_variant_builder_add (args, "{sv}", "thread", g_variant_new_int64 (page->thread_id));
  result = tandem_gui_device_call_plugin_sync (page->device, "sms", "messages", g_variant_builder_end (args), NULL);
  if (result != NULL)
    messages = g_variant_lookup_value (result, "messages", G_VARIANT_TYPE ("aa{sv}"));

  while ((child = gtk_widget_get_first_child (page->list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (page->list), child);
  page->first_date = 0;
  if (messages != NULL)
    {
      g_variant_iter_init (&iter, messages);
      while ((message = g_variant_iter_next_value (&iter)) != NULL)
        {
          gint64 date = 0;

          g_variant_lookup (message, "date", "x", &date);
          if (page->first_date == 0 || date < page->first_date)
            page->first_date = date;
          gtk_list_box_append (GTK_LIST_BOX (page->list), message_row_new (page, message));
          g_variant_unref (message);
          n++;
        }
    }
  gtk_widget_set_sensitive (page->older_button, n > 0);
  if (page->scroll_to_end)
    g_idle_add (scroll_to_end_cb, page);
}

static void
plugin_changed_cb (TandemGuiDevice *device, const char *plugin, gpointer user_data)
{
  if (g_str_equal (plugin, "sms"))
    rebuild_messages (user_data);
}

static void
send_cb (GtkWidget *widget, gpointer user_data)
{
  ConversationPage *page = user_data;
  const char *text = gtk_editable_get_text (GTK_EDITABLE (page->entry));
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  if (text == NULL || *text == '\0')
    return;
  g_variant_builder_add (args, "{sv}", "addresses", g_variant_new_strv ((const char *const *) page->addresses, -1));
  g_variant_builder_add (args, "{sv}", "body", g_variant_new_string (text));
  tandem_gui_device_call_plugin (page->device, "sms", "send", g_variant_builder_end (args));
  gtk_editable_set_text (GTK_EDITABLE (page->entry), "");
  page->scroll_to_end = TRUE;
}

static void
request_messages (ConversationPage *page, gint64 before)
{
  g_autoptr(GVariantBuilder) args = tandem_gui_args ();

  g_variant_builder_add (args, "{sv}", "thread", g_variant_new_int64 (page->thread_id));
  g_variant_builder_add (args, "{sv}", "before", g_variant_new_int64 (before));
  g_variant_builder_add (args, "{sv}", "count", g_variant_new_int32 (50));
  tandem_gui_device_call_plugin (page->device, "sms", "request-messages", g_variant_builder_end (args));
}

static void
older_cb (GtkButton *button, gpointer user_data)
{
  ConversationPage *page = user_data;

  page->scroll_to_end = FALSE;
  request_messages (page, page->first_date);
}

static void
conversation_page_free (gpointer data)
{
  ConversationPage *page = data;

  g_clear_signal_handler (&page->changed_id, page->device);
  g_strfreev (page->addresses);
  g_object_unref (page->device);
  g_free (page);
}

static GtkWidget *
conversation_page_new (TandemGuiDevice *device, GVariant *thread)
{
  ConversationPage *page = g_new0 (ConversationPage, 1);
  const char *title = "";
  gboolean loaded = FALSE;
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *entry_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *send_button = gtk_button_new_from_icon_name ("mail-send-symbolic");
  GtkWidget *nav_page;

  g_variant_lookup (thread, "id", "x", &page->thread_id);
  g_variant_lookup (thread, "title", "&s", &title);
  g_variant_lookup (thread, "loaded", "b", &loaded);
  g_variant_lookup (thread, "addresses", "^as", &page->addresses);
  page->device = g_object_ref (device);
  page->scroll_to_end = TRUE;

  page->older_button = gtk_button_new_with_label ("Load older messages");
  gtk_widget_add_css_class (page->older_button, "flat");
  gtk_widget_set_halign (page->older_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (page->older_button, 6);
  g_signal_connect (page->older_button, "clicked", G_CALLBACK (older_cb), page);
  gtk_box_append (GTK_BOX (box), page->older_button);

  page->list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (page->list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (page->list, "background");
  gtk_widget_set_vexpand (page->list, TRUE);
  gtk_widget_set_valign (page->list, GTK_ALIGN_END);
  page->scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (page->scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (page->scrolled), page->list);
  gtk_widget_set_vexpand (page->scrolled, TRUE);
  gtk_box_append (GTK_BOX (box), page->scrolled);

  page->entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (page->entry), "Message");
  gtk_widget_set_hexpand (page->entry, TRUE);
  g_signal_connect (page->entry, "activate", G_CALLBACK (send_cb), page);
  gtk_widget_add_css_class (send_button, "suggested-action");
  gtk_widget_add_css_class (send_button, "circular");
  g_signal_connect (send_button, "clicked", G_CALLBACK (send_cb), page);
  gtk_widget_set_margin_start (entry_box, 8);
  gtk_widget_set_margin_end (entry_box, 8);
  gtk_widget_set_margin_top (entry_box, 6);
  gtk_widget_set_margin_bottom (entry_box, 8);
  gtk_box_append (GTK_BOX (entry_box), page->entry);
  gtk_box_append (GTK_BOX (entry_box), send_button);
  gtk_box_append (GTK_BOX (box), entry_box);

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), adw_header_bar_new ());
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), box);
  nav_page = GTK_WIDGET (adw_navigation_page_new (toolbar, title));
  g_object_set_data_full (G_OBJECT (nav_page), "conversation-page", page, conversation_page_free);

  page->changed_id = g_signal_connect (device, "plugin-changed", G_CALLBACK (plugin_changed_cb), page);
  rebuild_messages (page);
  if (!loaded)
    request_messages (page, 0);
  return nav_page;
}

typedef struct
{
  TandemGuiDevice *device;
  GtkWidget *list;
  GtkWidget *status;
  gulong changed_id;
} ThreadsPage;

static void
threads_page_free (gpointer data)
{
  ThreadsPage *page = data;

  g_clear_signal_handler (&page->changed_id, page->device);
  g_object_unref (page->device);
  g_free (page);
}

static void
thread_row_activated_cb (GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
  ThreadsPage *page = user_data;
  GVariant *thread = g_object_get_data (G_OBJECT (row), "thread");
  GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (list), ADW_TYPE_NAVIGATION_VIEW);

  if (thread != NULL && view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (view), ADW_NAVIGATION_PAGE (conversation_page_new (page->device, thread)));
}

static void
rebuild_threads (ThreadsPage *page)
{
  g_autoptr(GVariant) state = tandem_gui_device_get_plugin_state (page->device, "sms");
  g_autoptr(GVariant) threads = state != NULL ? g_variant_lookup_value (state, "threads", G_VARIANT_TYPE ("aa{sv}")) : NULL;
  GtkWidget *child;
  GVariantIter iter;
  GVariant *thread;
  guint n = 0;

  while ((child = gtk_widget_get_first_child (page->list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (page->list), child);
  if (threads != NULL)
    {
      g_variant_iter_init (&iter, threads);
      while ((thread = g_variant_iter_next_value (&iter)) != NULL)
        {
          const char *title = "";
          g_autoptr(GVariant) last = g_variant_lookup_value (thread, "last", G_VARIANT_TYPE ("a{sv}"));
          const char *body = "";
          gint64 date = 0;
          gboolean read = TRUE, outgoing = TRUE;
          g_autofree char *snippet = NULL;
          g_autofree char *date_text = NULL;
          GtkWidget *row = adw_action_row_new ();
          GtkWidget *date_label;

          g_variant_lookup (thread, "title", "&s", &title);
          if (last != NULL)
            {
              g_variant_lookup (last, "body", "&s", &body);
              g_variant_lookup (last, "date", "x", &date);
              g_variant_lookup (last, "read", "b", &read);
              g_variant_lookup (last, "outgoing", "b", &outgoing);
            }
          snippet = g_strdup (body);
          g_strdelimit (snippet, "\n", ' ');
          date_text = date != 0 ? format_date (date) : g_strdup ("");
          date_label = gtk_label_new (date_text);
          adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
          adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
          adw_action_row_set_subtitle (ADW_ACTION_ROW (row), snippet);
          adw_action_row_set_subtitle_lines (ADW_ACTION_ROW (row), 1);
          gtk_widget_add_css_class (date_label, "dim-label");
          gtk_widget_add_css_class (date_label, "caption");
          adw_action_row_add_suffix (ADW_ACTION_ROW (row), date_label);
          if (!read && !outgoing)
            adw_action_row_add_prefix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("mail-unread-symbolic"));
          gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
          g_object_set_data_full (G_OBJECT (row), "thread", g_variant_ref (thread), (GDestroyNotify) g_variant_unref);
          gtk_list_box_append (GTK_LIST_BOX (page->list), row);
          g_variant_unref (thread);
          n++;
        }
    }
  gtk_widget_set_visible (page->status, n == 0);
}

static void
threads_changed_cb (TandemGuiDevice *device, const char *plugin, gpointer user_data)
{
  if (g_str_equal (plugin, "sms"))
    rebuild_threads (user_data);
}

static void
refresh_cb (GtkButton *button, gpointer user_data)
{
  ThreadsPage *page = user_data;

  tandem_gui_device_call_plugin (page->device, "sms", "threads", NULL);
}

GtkWidget *
tandem_gui_sms_page_new (TandemGuiDevice *device)
{
  ThreadsPage *page = g_new0 (ThreadsPage, 1);
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic");
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *clamp = adw_clamp_new ();
  GtkWidget *nav_page;

  page->device = g_object_ref (device);
  page->list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (page->list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (page->list, "boxed-list");
  gtk_widget_set_margin_top (page->list, 12);
  gtk_widget_set_margin_bottom (page->list, 12);
  gtk_widget_set_margin_start (page->list, 12);
  gtk_widget_set_margin_end (page->list, 12);
  g_signal_connect (page->list, "row-activated", G_CALLBACK (thread_row_activated_cb), page);
  page->status = adw_status_page_new ();
  adw_status_page_set_icon_name (ADW_STATUS_PAGE (page->status), "mail-unread-symbolic");
  adw_status_page_set_title (ADW_STATUS_PAGE (page->status), "No conversations yet");
  adw_status_page_set_description (ADW_STATUS_PAGE (page->status),
                                   "Conversations are fetched from the phone when it is connected and allows it.");
  gtk_widget_set_vexpand (page->status, TRUE);

  g_signal_connect (refresh, "clicked", G_CALLBACK (refresh_cb), page);
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), refresh);
  gtk_box_append (GTK_BOX (box), page->list);
  gtk_box_append (GTK_BOX (box), page->status);
  adw_clamp_set_child (ADW_CLAMP (clamp), box);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), clamp);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scrolled);
  nav_page = GTK_WIDGET (adw_navigation_page_new (toolbar, "Messages"));
  g_object_set_data_full (G_OBJECT (nav_page), "threads-page", page, threads_page_free);

  page->changed_id = g_signal_connect (device, "plugin-changed", G_CALLBACK (threads_changed_cb), page);
  rebuild_threads (page);
  if (tandem_gui_device_is_reachable (device))
    tandem_gui_device_call_plugin (device, "sms", "threads", NULL);
  return nav_page;
}

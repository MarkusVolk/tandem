/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../device.h"
#include "../plugins/sms.h"

#include <adwaita.h>

typedef struct
{
  TandemSmsPlugin *plugin;
  TandemSmsThread *thread;
  GtkWidget *list;
  GtkWidget *scrolled;
  GtkWidget *entry;
  GtkWidget *older_button;
  gulong updated_id;
  gboolean scroll_to_end;
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
  TandemSmsAttachment *attachment = g_object_get_data (G_OBJECT (button), "attachment");

  tandem_sms_plugin_request_attachment (page->plugin, attachment);
}

static GtkWidget *
message_row_new (ConversationPage *page, TandemSmsMessage *message)
{
  gboolean outgoing = tandem_sms_message_is_outgoing (message);
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *bubble = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *body = gtk_label_new (tandem_sms_message_get_body (message));
  g_autofree char *date = format_date (tandem_sms_message_get_date (message));
  GtkWidget *date_label = gtk_label_new (date);
  GPtrArray *attachments = tandem_sms_message_get_attachments (message);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_add_css_class (bubble, "card");
  gtk_widget_add_css_class (bubble, outgoing ? "tandem-outgoing" : "tandem-incoming");
  gtk_widget_set_halign (bubble, outgoing ? GTK_ALIGN_END : GTK_ALIGN_START);
  gtk_widget_set_margin_top (bubble, 4);
  gtk_widget_set_margin_bottom (bubble, 4);
  gtk_widget_set_margin_start (bubble, outgoing ? 60 : 8);
  gtk_widget_set_margin_end (bubble, outgoing ? 8 : 60);
  g_object_set (bubble, "margin-top", 4, NULL);
  gtk_label_set_wrap (GTK_LABEL (body), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (body), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign (GTK_LABEL (body), 0);
  gtk_label_set_selectable (GTK_LABEL (body), TRUE);
  gtk_widget_set_margin_start (body, 10);
  gtk_widget_set_margin_end (body, 10);
  gtk_widget_set_margin_top (body, 8);
  if (*tandem_sms_message_get_body (message) != '\0')
    gtk_box_append (GTK_BOX (bubble), body);

  for (guint i = 0; i < attachments->len; i++)
    {
      TandemSmsAttachment *attachment = g_ptr_array_index (attachments, i);
      GtkWidget *button = gtk_button_new ();
      GtkWidget *child = NULL;

      if (attachment->thumbnail != NULL)
        {
          g_autoptr(GdkTexture) texture = gdk_texture_new_from_bytes (attachment->thumbnail, NULL);

          if (texture != NULL)
            {
              child = gtk_picture_new_for_paintable (GDK_PAINTABLE (texture));
              gtk_picture_set_can_shrink (GTK_PICTURE (child), TRUE);
              gtk_widget_set_size_request (child, 160, 120);
            }
        }
      if (child == NULL)
        child = gtk_label_new (attachment->mime_type);
      gtk_button_set_child (GTK_BUTTON (button), child);
      gtk_widget_add_css_class (button, "flat");
      gtk_widget_set_margin_start (button, 6);
      gtk_widget_set_margin_end (button, 6);
      g_object_set_data (G_OBJECT (button), "attachment", attachment);
      g_signal_connect (button, "clicked", G_CALLBACK (attachment_clicked_cb), page);
      gtk_box_append (GTK_BOX (bubble), button);
    }

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
  GListModel *messages = tandem_sms_thread_get_messages (page->thread);
  guint n = g_list_model_get_n_items (messages);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (page->list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (page->list), child);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemSmsMessage) message = g_list_model_get_item (messages, i);

      gtk_list_box_append (GTK_LIST_BOX (page->list), message_row_new (page, message));
    }
  gtk_widget_set_sensitive (page->older_button, n > 0);
  if (page->scroll_to_end)
    g_idle_add (scroll_to_end_cb, page);
}

static void
thread_updated_cb (TandemSmsPlugin *plugin, TandemSmsThread *thread, gpointer user_data)
{
  ConversationPage *page = user_data;

  if (thread == page->thread)
    rebuild_messages (page);
}

static void
send_cb (GtkWidget *widget, gpointer user_data)
{
  ConversationPage *page = user_data;
  const char *text = gtk_editable_get_text (GTK_EDITABLE (page->entry));
  TandemSmsMessage *last = tandem_sms_thread_get_last (page->thread);
  gint64 sub_id = -1;

  if (text == NULL || *text == '\0')
    return;
  if (last != NULL)
    sub_id = -1;
  tandem_sms_plugin_send (page->plugin, tandem_sms_thread_get_addresses (page->thread), text, sub_id);
  gtk_editable_set_text (GTK_EDITABLE (page->entry), "");
  page->scroll_to_end = TRUE;
}

static void
older_cb (GtkButton *button, gpointer user_data)
{
  ConversationPage *page = user_data;
  GListModel *messages = tandem_sms_thread_get_messages (page->thread);
  g_autoptr(TandemSmsMessage) first = g_list_model_get_n_items (messages) > 0 ? g_list_model_get_item (messages, 0) : NULL;

  page->scroll_to_end = FALSE;
  tandem_sms_plugin_request_messages (page->plugin, page->thread, first != NULL ? tandem_sms_message_get_date (first) : 0, 50);
}

static void
conversation_page_free (gpointer data)
{
  ConversationPage *page = data;

  g_clear_signal_handler (&page->updated_id, page->plugin);
  g_object_unref (page->thread);
  g_object_unref (page->plugin);
  g_free (page);
}

static GtkWidget *
conversation_page_new (TandemSmsPlugin *plugin, TandemSmsThread *thread)
{
  ConversationPage *page = g_new0 (ConversationPage, 1);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (plugin));
  g_autofree char *title = tandem_sms_thread_get_title (thread, device);
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *entry_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *send_button = gtk_button_new_from_icon_name ("mail-send-symbolic");
  GtkWidget *nav_page;

  page->plugin = g_object_ref (plugin);
  page->thread = g_object_ref (thread);
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

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), box);
  nav_page = GTK_WIDGET (adw_navigation_page_new (toolbar, title));
  g_object_set_data_full (G_OBJECT (nav_page), "conversation-page", page, conversation_page_free);

  page->updated_id = g_signal_connect (plugin, "thread-updated", G_CALLBACK (thread_updated_cb), page);
  rebuild_messages (page);
  if (!tandem_sms_thread_is_loaded (thread))
    tandem_sms_plugin_request_messages (plugin, thread, 0, 50);
  return nav_page;
}

typedef struct
{
  TandemSmsPlugin *plugin;
  GtkWidget *list;
  GtkWidget *status;
  gulong changed_id;
} ThreadsPage;

static void
threads_page_free (gpointer data)
{
  ThreadsPage *page = data;

  g_clear_signal_handler (&page->changed_id, page->plugin);
  g_object_unref (page->plugin);
  g_free (page);
}

static void
thread_row_activated_cb (GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
  ThreadsPage *page = user_data;
  TandemSmsThread *thread = g_object_get_data (G_OBJECT (row), "thread");
  GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (list), ADW_TYPE_NAVIGATION_VIEW);

  if (thread != NULL && view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (view), ADW_NAVIGATION_PAGE (conversation_page_new (page->plugin, thread)));
}

static void
rebuild_threads (ThreadsPage *page)
{
  GListModel *threads = tandem_sms_plugin_get_threads (page->plugin);
  TandemDevice *device = tandem_plugin_get_device (TANDEM_PLUGIN (page->plugin));
  guint n = g_list_model_get_n_items (threads);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (page->list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (page->list), child);
  for (guint i = 0; i < n; i++)
    {
      g_autoptr(TandemSmsThread) thread = g_list_model_get_item (threads, i);
      TandemSmsMessage *last = tandem_sms_thread_get_last (thread);
      g_autofree char *title = tandem_sms_thread_get_title (thread, device);
      g_autofree char *date = last != NULL ? format_date (tandem_sms_message_get_date (last)) : g_strdup ("");
      g_autofree char *snippet = last != NULL ? g_strdup (tandem_sms_message_get_body (last)) : g_strdup ("");
      GtkWidget *row = adw_action_row_new ();
      GtkWidget *date_label = gtk_label_new (date);

      g_strdelimit (snippet, "\n", ' ');
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (row), snippet);
      adw_action_row_set_subtitle_lines (ADW_ACTION_ROW (row), 1);
      adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
      gtk_widget_add_css_class (date_label, "dim-label");
      gtk_widget_add_css_class (date_label, "caption");
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), date_label);
      if (last != NULL && !tandem_sms_message_is_read (last) && !tandem_sms_message_is_outgoing (last))
        adw_action_row_add_prefix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("mail-unread-symbolic"));
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
      g_object_set_data_full (G_OBJECT (row), "thread", g_object_ref (thread), g_object_unref);
      gtk_list_box_append (GTK_LIST_BOX (page->list), row);
    }
  gtk_widget_set_visible (page->status, n == 0);
}

static void
threads_changed_cb (TandemSmsPlugin *plugin, gpointer user_data)
{
  rebuild_threads (user_data);
}

static void
refresh_cb (GtkButton *button, gpointer user_data)
{
  ThreadsPage *page = user_data;

  tandem_sms_plugin_request_threads (page->plugin);
}

GtkWidget *
tandem_sms_page_new (TandemSmsPlugin *plugin)
{
  ThreadsPage *page = g_new0 (ThreadsPage, 1);
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic");
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *clamp = adw_clamp_new ();
  GtkWidget *nav_page;

  page->plugin = g_object_ref (plugin);
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
  adw_status_page_set_description (ADW_STATUS_PAGE (page->status), "Conversations are fetched from the phone when it is connected.");
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

  page->changed_id = g_signal_connect (plugin, "threads-changed", G_CALLBACK (threads_changed_cb), page);
  rebuild_threads (page);
  if (tandem_device_is_reachable (tandem_plugin_get_device (TANDEM_PLUGIN (plugin))) &&
      g_list_model_get_n_items (tandem_sms_plugin_get_threads (plugin)) == 0)
    tandem_sms_plugin_request_threads (plugin);
  return nav_page;
}

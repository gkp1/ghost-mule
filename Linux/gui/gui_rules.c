#include "gui.h"

static GtkWidget *rules_list_box = NULL;
static GtkWidget *btn_select_all_header = NULL;

// forward decl
static void refresh_rules_ui();

static void free_rule_data(RuleData *rule) {
    if (rule) {
        if (rule->process_name) free(rule->process_name);
        if (rule->target_hosts) free(rule->target_hosts);
        if (rule->target_ports) free(rule->target_ports);
        free(rule);
    }
}

static void on_rule_delete(GtkWidget *widget, gpointer data) {
    RuleData *rule = (RuleData *)data;
    ProxyBridge_DeleteRule(rule->id);
    g_rules_list = g_list_remove(g_rules_list, rule);
    free_rule_data(rule);
    save_config();
    refresh_rules_ui();
}

static void on_rule_toggle(GtkToggleButton *btn, gpointer data) {
    RuleData *rule = (RuleData *)data;
    rule->enabled = gtk_toggle_button_get_active(btn);
    if (rule->enabled) ProxyBridge_EnableRule(rule->id);
    else ProxyBridge_DisableRule(rule->id);
    save_config();
}

static void on_save_rule(GtkWidget *widget, gpointer data) {
    GtkWidget **widgets = (GtkWidget **)data;
    GtkWidget *dialog = widgets[0];
    RuleData *edit_rule = (RuleData *)widgets[6]; // existing rule if present
    
    const char *proc = gtk_entry_get_text(GTK_ENTRY(widgets[1]));
    const char *hosts = gtk_entry_get_text(GTK_ENTRY(widgets[2]));
    const char *ports = gtk_entry_get_text(GTK_ENTRY(widgets[3]));
    RuleProtocol proto = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets[4]));
    RuleAction action = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets[5]));

    if (strlen(proc) == 0) return; // need process name
    
    if (edit_rule) {
        // update backend
        ProxyBridge_EditRule(edit_rule->id, proc, hosts, ports, proto, action);
        // update local
        free(edit_rule->process_name); edit_rule->process_name = strdup(proc);
        free(edit_rule->target_hosts); edit_rule->target_hosts = strdup(hosts);
        free(edit_rule->target_ports); edit_rule->target_ports = strdup(ports);
        edit_rule->protocol = proto;
        edit_rule->action = action;
    } else {
        // new rule
        uint32_t new_id = ProxyBridge_AddRule(proc, hosts, ports, proto, action);
        RuleData *new_rule = malloc(sizeof(RuleData));
        new_rule->id = new_id;
        new_rule->process_name = strdup(proc);
        new_rule->target_hosts = strdup(hosts);
        new_rule->target_ports = strdup(ports);
        new_rule->protocol = proto;
        new_rule->action = action;
        new_rule->enabled = true; 
        new_rule->selected = false;
        g_rules_list = g_list_append(g_rules_list, new_rule);
    }
    save_config();
    refresh_rules_ui();
    gtk_widget_destroy(dialog);
    free(widgets);
}

static void on_browse_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *entry = (GtkWidget *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Application",
                                      NULL,
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Select", GTK_RESPONSE_ACCEPT,
                                      NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *base = g_path_get_basename(filename);
        gtk_entry_set_text(GTK_ENTRY(entry), base);
        g_free(base);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void open_rule_dialog(RuleData *rule) {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), rule ? "Edit Rule" : "Add Rule");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    
    // process input
    GtkWidget *proc_entry = gtk_entry_new();
    GtkWidget *browse_btn = gtk_button_new_with_label("Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_clicked), proc_entry);
    GtkWidget *proc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(proc_box), proc_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(proc_box), browse_btn, FALSE, FALSE, 0);

    GtkWidget *host_entry = gtk_entry_new();
    GtkWidget *port_entry = gtk_entry_new();
    
    // protocol list
    GtkWidget *proto_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "TCP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "UDP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(proto_combo), "BOTH");
    
    // action list
    GtkWidget *action_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "PROXY");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "DIRECT");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "BLOCK");

    if (rule) {
        gtk_entry_set_text(GTK_ENTRY(proc_entry), rule->process_name);
        gtk_entry_set_text(GTK_ENTRY(host_entry), rule->target_hosts);
        gtk_entry_set_text(GTK_ENTRY(port_entry), rule->target_ports);
        gtk_combo_box_set_active(GTK_COMBO_BOX(proto_combo), rule->protocol);
        gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo), rule->action);
    } else {
        gtk_entry_set_text(GTK_ENTRY(port_entry), "*"); 
        gtk_entry_set_text(GTK_ENTRY(host_entry), "*"); 
        gtk_combo_box_set_active(GTK_COMBO_BOX(proto_combo), 2); // BOTH default
        gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo), 0); // PROXY default
    }

    int row = 0;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Process Name:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), proc_box, 1, row, 1, 1); row++;
    
    GtkWidget *proc_hint = gtk_label_new("Example: firefox; chrome; /usr/bin/wget");
    gtk_style_context_add_class(gtk_widget_get_style_context(proc_hint), "dim-label");
    gtk_widget_set_halign(proc_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), proc_hint, 1, row, 1, 1); row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target Host:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), host_entry, 1, row, 1, 1); row++;
    
    GtkWidget *host_hint = gtk_label_new("Example: 192.168.1.*; 10.0.0.1-50; *");
    gtk_style_context_add_class(gtk_widget_get_style_context(host_hint), "dim-label");
    gtk_widget_set_halign(host_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), host_hint, 1, row, 1, 1); row++;

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Target Port:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), port_entry, 1, row, 1, 1); row++;
    
    GtkWidget *port_hint = gtk_label_new("Example: 80; 443; 8000-8080; *");
    gtk_style_context_add_class(gtk_widget_get_style_context(port_hint), "dim-label");
    gtk_widget_set_halign(port_hint, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), port_hint, 1, row, 1, 1); row++;
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Protocol:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), proto_combo, 1, row, 1, 1); row++;
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Action:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_combo, 1, row, 1, 1); row++;

    gtk_container_add(GTK_CONTAINER(content), grid);
    
    GtkWidget *save_btn = gtk_button_new_with_label("Save");
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), cancel_btn, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), save_btn, GTK_RESPONSE_ACCEPT);
    
    GtkWidget **data = malloc(7 * sizeof(GtkWidget*));
    data[0] = dialog;
    data[1] = proc_entry;
    data[2] = host_entry;
    data[3] = port_entry;
    data[4] = proto_combo;
    data[5] = action_combo;
    data[6] = (GtkWidget*)rule;
    
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_rule), data);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(gtk_widget_destroy), NULL); 
    gtk_widget_show_all(dialog);
}

static void on_rule_edit(GtkWidget *widget, gpointer data) {
    on_proxy_rules_clicked(NULL, NULL); // Re-open if closed? Usually modal.
    // Wait, on_rule_edit is clicked FROM the rules list.
    open_rule_dialog((RuleData *)data);
}

static void on_rule_add_clicked(GtkWidget *widget, gpointer data) {
    open_rule_dialog(NULL);
}

static void on_rule_select_toggle(GtkToggleButton *btn, gpointer data) {
    RuleData *rule = (RuleData *)data;
    rule->selected = gtk_toggle_button_get_active(btn);
    if (btn_select_all_header) {
         bool all_selected = (g_rules_list != NULL);
         if (g_rules_list == NULL) all_selected = false;
         for (GList *l = g_rules_list; l != NULL; l = l->next) {
            RuleData *r = (RuleData *)l->data;
            if (!r->selected) {
                all_selected = false;
                break;
            }
        }
        gtk_button_set_label(GTK_BUTTON(btn_select_all_header), all_selected ? "Deselect All" : "Select All");
    }
}

static void on_rule_export_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;
    bool any_selected = false;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        if (((RuleData *)l->data)->selected) { any_selected = true; break; }
    }
    if (!any_selected) {
        show_message(NULL, GTK_MESSAGE_WARNING, "Please select at least one rule to export.");
        return;
    }
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export Rules", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "proxy_rules.json");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "[\n");
            bool first = true;
            for (GList *l = g_rules_list; l != NULL; l = l->next) {
                RuleData *r = (RuleData *)l->data;
                if (!r->selected) continue;
                if (!first) fprintf(f, ",\n");
                char *proc = escape_json_string(r->process_name);
                char *host = escape_json_string(r->target_hosts);
                char *port = escape_json_string(r->target_ports);
                const char *proto = (r->protocol == RULE_PROTOCOL_TCP) ? "TCP" : (r->protocol == RULE_PROTOCOL_UDP ? "UDP" : "BOTH");
                const char *act = (r->action == RULE_ACTION_PROXY) ? "PROXY" : (r->action == RULE_ACTION_DIRECT ? "DIRECT" : "BLOCK");
                fprintf(f, "  {\n    \"processNames\": \"%s\",\n    \"targetHosts\": \"%s\",\n    \"targetPorts\": \"%s\",\n    \"protocol\": \"%s\",\n    \"action\": \"%s\",\n    \"enabled\": %s\n  }", proc, host, port, proto, act, r->enabled ? "true" : "false");
                g_free(proc); g_free(host); g_free(port);
                first = false;
            }
            fprintf(f, "\n]\n");
            fclose(f);
            show_message(NULL, GTK_MESSAGE_INFO, "Rules exported successfully.");
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_rule_import_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Import Rules", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *content = NULL;
        gsize len;
        if (g_file_get_contents(filename, &content, &len, NULL)) {
            char *curr = content;
            int imported = 0;
            while ((curr = strchr(curr, '{')) != NULL) {
                char *end = strchr(curr, '}');
                if (!end) break;
                char saved = *end; *end = '\0';
                
                char *proc = extract_sub_json_str(curr, "processNames");
                char *host = extract_sub_json_str(curr, "targetHosts");
                char *port = extract_sub_json_str(curr, "targetPorts");
                char *proto_s = extract_sub_json_str(curr, "protocol");
                char *act_s = extract_sub_json_str(curr, "action");
                bool en = extract_sub_json_bool(curr, "enabled");
                
                if (proc && host && port && proto_s && act_s) {
                    RuleProtocol p = RULE_PROTOCOL_BOTH;
                    if (strcmp(proto_s, "TCP") == 0) p = RULE_PROTOCOL_TCP;
                    else if (strcmp(proto_s, "UDP") == 0) p = RULE_PROTOCOL_UDP;
                    RuleAction a = RULE_ACTION_PROXY;
                    if (strcmp(act_s, "DIRECT") == 0) a = RULE_ACTION_DIRECT;
                    else if (strcmp(act_s, "BLOCK") == 0) a = RULE_ACTION_BLOCK;
                    
                    uint32_t nid = ProxyBridge_AddRule(proc, host, port, p, a);
                     if (!en) ProxyBridge_DisableRule(nid);
                    RuleData *nd = malloc(sizeof(RuleData));
                    nd->id = nid; nd->process_name = strdup(proc); nd->target_hosts = strdup(host);
                    nd->target_ports = strdup(port); nd->protocol = p; nd->action = a; nd->enabled = en; nd->selected = false;
                    g_rules_list = g_list_append(g_rules_list, nd);
                    imported++;
                }
                g_free(proc); g_free(host); g_free(port); g_free(proto_s); g_free(act_s);
                *end = saved; curr = end + 1;
            }
            g_free(content);
            if (imported > 0) { refresh_rules_ui(); show_message(NULL, GTK_MESSAGE_INFO, "Imported %d rules.", imported); }
        }
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_bulk_delete_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;
    GList *iter = g_rules_list;
    GList *to_delete = NULL;
    while (iter != NULL) {
        RuleData *rule = (RuleData *)iter->data;
        if (rule->selected) to_delete = g_list_append(to_delete, rule);
        iter = iter->next;
    }
    if (!to_delete) return;
    for (GList *d = to_delete; d != NULL; d = d->next) {
        RuleData *rule = (RuleData *)d->data;
        ProxyBridge_DeleteRule(rule->id);
        g_rules_list = g_list_remove(g_rules_list, rule);
        free_rule_data(rule);
    }
    g_list_free(to_delete);
    refresh_rules_ui();
}

static void on_select_all_clicked(GtkWidget *widget, gpointer data) {
    if (!g_rules_list) return;
    bool all_selected = true;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        if (!((RuleData *)l->data)->selected) { all_selected = false; break; }
    }
    bool new_state = !all_selected;
    for (GList *l = g_rules_list; l != NULL; l = l->next) ((RuleData *)l->data)->selected = new_state;
    refresh_rules_ui();
}

static void refresh_rules_ui() {
    if (!rules_list_box) return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(rules_list_box));
    for(GList *iter = children; iter != NULL; iter = g_list_next(iter)) gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    if (btn_select_all_header) {
        bool all_selected = (g_rules_list != NULL);
        if (g_rules_list == NULL) all_selected = false;
        for (GList *l = g_rules_list; l != NULL; l = l->next) {
            if (!((RuleData *)l->data)->selected) { all_selected = false; break; }
        }
        gtk_button_set_label(GTK_BUTTON(btn_select_all_header), all_selected ? "Deselect All" : "Select All");
    }
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
    gtk_container_add(GTK_CONTAINER(rules_list_box), grid);

    int row = 0;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("  "), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Enable"), 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Actions"), 2, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("SR"), 3, row, 1, 1);
    GtkWidget *h_proc = gtk_label_new("Process"); gtk_widget_set_halign(h_proc, GTK_ALIGN_START);
    gtk_widget_set_hexpand(h_proc, TRUE); gtk_grid_attach(GTK_GRID(grid), h_proc, 4, row, 1, 1);
    GtkWidget *h_host = gtk_label_new("Target Hosts"); gtk_widget_set_halign(h_host, GTK_ALIGN_START);
    gtk_widget_set_hexpand(h_host, TRUE); gtk_grid_attach(GTK_GRID(grid), h_host, 5, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Protocol"), 6, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Action"), 7, row, 1, 1);
    row++;
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row, 8, 1); row++;

    int sr_counter = 1;
    for (GList *l = g_rules_list; l != NULL; l = l->next) {
        RuleData *r = (RuleData *)l->data;
        GtkWidget *chk_sel = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_sel), r->selected);
        g_signal_connect(chk_sel, "toggled", G_CALLBACK(on_rule_select_toggle), r);
        gtk_grid_attach(GTK_GRID(grid), chk_sel, 0, row, 1, 1);

        GtkWidget *chk = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), r->enabled);
        g_signal_connect(chk, "toggled", G_CALLBACK(on_rule_toggle), r);
        gtk_grid_attach(GTK_GRID(grid), chk, 1, row, 1, 1);
        
        GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *btn_edit = gtk_button_new_with_label("Edit");
        g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_rule_edit), r);
        GtkWidget *btn_del = gtk_button_new_with_label("Delete");
        g_signal_connect(btn_del, "clicked", G_CALLBACK(on_rule_delete), r);
        gtk_box_pack_start(GTK_BOX(act_box), btn_edit, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(act_box), btn_del, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), act_box, 2, row, 1, 1);
        
        char sr_str[16]; snprintf(sr_str, sizeof(sr_str), "%d", sr_counter++);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(sr_str), 3, row, 1, 1);
        
        GtkWidget *l_proc = gtk_label_new(r->process_name); gtk_widget_set_halign(l_proc, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(l_proc), PANGO_ELLIPSIZE_END);
        gtk_grid_attach(GTK_GRID(grid), l_proc, 4, row, 1, 1);
        
        GtkWidget *l_host = gtk_label_new(r->target_hosts); gtk_widget_set_halign(l_host, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(l_host), PANGO_ELLIPSIZE_END);
        gtk_grid_attach(GTK_GRID(grid), l_host, 5, row, 1, 1);
        
        const char* proto_strs[] = {"TCP", "UDP", "BOTH"};
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(proto_strs[r->protocol]), 6, row, 1, 1);
        
        const char* action_strs[] = {"PROXY", "DIRECT", "BLOCK"};
        GtkWidget *l_act = gtk_label_new(action_strs[r->action]);
        GtkStyleContext *context = gtk_widget_get_style_context(l_act);
        if (r->action == RULE_ACTION_PROXY) gtk_style_context_add_class(context, "success");
        else if (r->action == RULE_ACTION_DIRECT) gtk_style_context_add_class(context, "info");
        else gtk_style_context_add_class(context, "warning");
        gtk_grid_attach(GTK_GRID(grid), l_act, 7, row, 1, 1);
        
        row++;
        gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, row, 8, 1); row++;
    }
    gtk_widget_show_all(rules_list_box);
}

void on_proxy_rules_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dialog), "Process Rules");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 500);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(dialog), vbox);
    
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Process Rules</span>");
    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Rule");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_rule_add_clicked), NULL);
    btn_select_all_header = gtk_button_new_with_label("Select All");
    g_signal_connect(btn_select_all_header, "clicked", G_CALLBACK(on_select_all_clicked), NULL);
    GtkWidget *del_all_btn = gtk_button_new_with_label("Delete Selected");
    g_signal_connect(del_all_btn, "clicked", G_CALLBACK(on_bulk_delete_clicked), NULL);
    GtkWidget *import_btn = gtk_button_new_with_label("Import");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_rule_import_clicked), NULL);
    GtkWidget *export_btn = gtk_button_new_with_label("Export");
    g_signal_connect(export_btn, "clicked", G_CALLBACK(on_rule_export_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(header_box), title, FALSE, FALSE, 0);
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header_box), spacer, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(header_box), add_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), btn_select_all_header, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), del_all_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), export_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(header_box), import_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    rules_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(scrolled), rules_list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
    
    refresh_rules_ui();
    gtk_widget_show_all(dialog);
}

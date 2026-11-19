/**************************************************************************/
/*  horror_ai_assistant_editor_plugin.h                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            MINA ENGINE                                  */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

#include "core/io/config_file.h"
#include "core/io/http_client.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class Button;
class EditorDock;
class FileDialog;
class HBoxContainer;
class HTTPRequest;
class Label;
class LineEdit;
class Node;
class ItemList;
class PanelContainer;
class RichTextLabel;
class ScrollContainer;
class TextEdit;
class VBoxContainer;
class Window;

class HorrorAIAssistantEditorPlugin : public EditorPlugin {
	GDCLASS(HorrorAIAssistantEditorPlugin, EditorPlugin);

	EditorDock *dock = nullptr;
	ScrollContainer *transcript_scroll = nullptr;
	VBoxContainer *message_list = nullptr;
	FileDialog *image_file_dialog = nullptr;
	HBoxContainer *attachment_row = nullptr;
	TextEdit *prompt_input = nullptr;
	Label *status_label = nullptr;
	Label *session_title = nullptr;
	Label *attachment_label = nullptr;
	Button *send_button = nullptr;
	Button *activity_button = nullptr;
	Button *clear_attachments_button = nullptr;
	Button *history_button = nullptr;
	Window *settings_window = nullptr;
	Window *session_history_window = nullptr;
	ItemList *session_history_list = nullptr;
	PanelContainer *activity_card = nullptr;
	TextEdit *activity_details = nullptr;
	RichTextLabel *streaming_body = nullptr;
	VBoxContainer *streaming_message = nullptr;
	LineEdit *endpoint_input = nullptr;
	LineEdit *deployment_input = nullptr;
	LineEdit *api_key_input = nullptr;

	Array responses_input_items;
	Array activity_entries;
	Array pending_image_paths;
	Array conversation_messages;
	Array session_ids;
	String system_instructions;
	String current_session_id;
	String stream_request_path;
	String stream_text;
	Dictionary staged_files;
	String api_key;
	String last_request_url;
	Ref<ConfigFile> saved_settings;
	Ref<ConfigFile> session_storage;
	Ref<HTTPClient> stream_client;
	Vector<String> stream_headers;
	PackedByteArray stream_request_body;
	PackedByteArray stream_buffer;
	PackedByteArray stream_error_body;
	PackedByteArray stream_raw_body;
	int tool_round = 0;
	int stream_response_code = 0;
	uint64_t stream_started_msec = 0;
	bool busy = false;
	bool restoring_session = false;
	bool stream_request_sent = false;

	void _build_panel();
	void _build_settings_window();
	void _build_session_history_window();
	void _load_session_history();
	void _refresh_session_history_list();
	void _show_session_history();
	void _open_selected_session();
	void _load_session_from_history_item(int p_index);
	void _load_session(const String &p_session_id);
	void _save_current_session();
	void _poll_stream();
	void _consume_stream_bytes(const PackedByteArray &p_bytes);
	void _handle_stream_line(const PackedByteArray &p_line);
	void _finish_stream();
	void _fail_stream(const String &p_error);
	void _close_stream_transport();
	void _begin_streaming_reply();
	void _update_streaming_reply(const String &p_text);
	void _clear_streaming_reply();
	void _open_image_dialog();
	void _on_images_selected(const PackedStringArray &p_paths);
	void _clear_image_attachments();
	void _refresh_image_attachments();
	void _load_settings();
	void _start_new_session();
	void _append_message(const String &p_role, const String &p_text, const Array &p_image_paths = Array());
	void _copy_message(const String &p_text);
	void _edit_message(const String &p_text, const Array &p_image_paths);
	void _send_prompt();
	void _request_next_turn();
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _handle_responses_response(const Dictionary &p_response);
	void _set_busy(bool p_busy, const String &p_status = String());
	void _record_tool_activity(const String &p_name, const Dictionary &p_arguments, const Dictionary &p_result);
	void _refresh_activity_view();
	void _toggle_activity_details();
	void _discard_staged_changes();
	void _show_settings();
	void _save_settings();
	void _apply_changes();

	Array _get_tool_schemas() const;
	Dictionary _execute_tool(const String &p_name, const Dictionary &p_arguments);
	Dictionary _list_project_files(const String &p_directory, bool p_audio_only) const;
	void _collect_project_files(const String &p_directory, Array &r_files, int p_limit, bool p_audio_only, int &r_count) const;
	Dictionary _read_project_file(const String &p_path) const;
	Dictionary _stage_project_file(const String &p_path, const String &p_content, const String &p_reason);
	Dictionary _inspect_current_scene() const;
	void _collect_scene_nodes(Node *p_node, const String &p_path, Array &r_nodes, int p_limit) const;
	bool _is_safe_project_path(const String &p_path, bool p_allow_directory) const;
	bool _is_allowed_text_path(const String &p_path) const;
	bool _is_ignored_project_path(const String &p_path) const;
	String _get_setting(const String &p_name, const String &p_default) const;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	HorrorAIAssistantEditorPlugin() = default;
};

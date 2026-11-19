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

#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class Button;
class ConfirmationDialog;
class EditorDock;
class HTTPRequest;
class Label;
class LineEdit;
class Node;
class RichTextLabel;
class TextEdit;
class Window;

class HorrorAIAssistantEditorPlugin : public EditorPlugin {
	GDCLASS(HorrorAIAssistantEditorPlugin, EditorPlugin);

	EditorDock *dock = nullptr;
	HTTPRequest *http_request = nullptr;
	RichTextLabel *transcript = nullptr;
	TextEdit *prompt_input = nullptr;
	Label *status_label = nullptr;
	Label *session_title = nullptr;
	Button *send_button = nullptr;
	Button *apply_button = nullptr;
	Window *settings_window = nullptr;
	TextEdit *change_preview = nullptr;
	LineEdit *endpoint_input = nullptr;
	LineEdit *deployment_input = nullptr;
	LineEdit *api_version_input = nullptr;
	LineEdit *api_key_input = nullptr;
	ConfirmationDialog *apply_dialog = nullptr;

	Array messages;
	Dictionary staged_files;
	String api_key;
	int tool_round = 0;
	bool busy = false;

	void _build_panel();
	void _build_settings_window();
	void _start_new_session();
	void _append_message(const String &p_role, const String &p_text);
	void _send_prompt();
	void _request_next_turn();
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _set_busy(bool p_busy, const String &p_status = String());
	void _show_settings();
	void _save_settings();
	void _update_pending_changes();
	void _confirm_apply_changes();
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

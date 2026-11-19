/**************************************************************************/
/*  horror_ai_assistant_editor_plugin.cpp                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            MINA ENGINE                                  */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#include "horror_ai_assistant_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"

namespace {

Dictionary _schema_string(const String &p_description) {
	Dictionary schema;
	schema["type"] = "string";
	schema["description"] = p_description;
	return schema;
}

Dictionary _function_tool(const String &p_name, const String &p_description, const Dictionary &p_properties, const Array &p_required) {
	Dictionary parameters;
	parameters["type"] = "object";
	parameters["properties"] = p_properties;
	parameters["required"] = p_required;
	parameters["additionalProperties"] = false;

	Dictionary function;
	function["name"] = p_name;
	function["description"] = p_description;
	function["parameters"] = parameters;

	Dictionary tool;
	tool["type"] = "function";
	tool["function"] = function;
	return tool;
}

} // namespace

void HorrorAIAssistantEditorPlugin::_build_panel() {
	dock = memnew(EditorDock);
	dock->set_name("MinaAIAssistantDock");
	dock->set_title(String::utf8("Mina AI"));
	dock->set_layout_key("mina_ai_assistant");
	dock->set_icon_name("MinaEngine");
	dock->set_default_slot(EditorDock::DOCK_SLOT_RIGHT_BR);
	dock->set_custom_minimum_size(Size2(340, 0) * EDSCALE);

	VBoxContainer *layout = memnew(VBoxContainer);
	dock->add_child(layout);

	HBoxContainer *header = memnew(HBoxContainer);
	session_title = memnew(Label);
	session_title->set_text("New Session");
	session_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header->add_child(session_title);
	Button *new_session_button = memnew(Button);
	new_session_button->set_text("+");
	new_session_button->set_tooltip_text(String::utf8("新しいセッション"));
	new_session_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_start_new_session));
	header->add_child(new_session_button);
	Button *settings_button = memnew(Button);
	settings_button->set_text("⚙");
	settings_button->set_tooltip_text(String::utf8("Azure OpenAI設定"));
	settings_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_show_settings));
	header->add_child(settings_button);
	layout->add_child(header);
	layout->add_child(memnew(HSeparator));

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	layout->add_child(scroll);
	transcript = memnew(RichTextLabel);
	transcript->set_use_bbcode(false);
	transcript->set_fit_content(true);
	transcript->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	transcript->set_custom_minimum_size(Size2(0, 220) * EDSCALE);
	scroll->add_child(transcript);

	apply_button = memnew(Button);
	apply_button->set_text(String::utf8("AIの変更案を確認"));
	apply_button->set_visible(false);
	apply_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_confirm_apply_changes));
	layout->add_child(apply_button);

	status_label = memnew(Label);
	status_label->set_text(String::utf8("Azure OpenAI未設定"));
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(status_label);

	prompt_input = memnew(TextEdit);
	prompt_input->set_placeholder(String::utf8("作りたいゲームや怖い演出を説明してください…"));
	prompt_input->set_custom_minimum_size(Size2(0, 88) * EDSCALE);
	prompt_input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	layout->add_child(prompt_input);

	HBoxContainer *input_footer = memnew(HBoxContainer);
	Label *privacy_note = memnew(Label);
	privacy_note->set_text(String::utf8("変更は確認後に適用"));
	privacy_note->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_footer->add_child(privacy_note);
	send_button = memnew(Button);
	send_button->set_text(String::utf8("送信 ↑"));
	send_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_send_prompt));
	input_footer->add_child(send_button);
	layout->add_child(input_footer);

	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(120.0);
	dock->add_child(http_request);
	http_request->connect("request_completed", callable_mp(this, &HorrorAIAssistantEditorPlugin::_on_request_completed));

	apply_dialog = memnew(ConfirmationDialog);
	apply_dialog->set_title(String::utf8("AIが作成した変更を適用"));
	apply_dialog->set_ok_button_text(String::utf8("適用"));
	apply_dialog->set_cancel_button_text(String::utf8("戻る"));
	dock->add_child(apply_dialog);
	apply_dialog->get_label()->hide();
	change_preview = memnew(TextEdit);
	change_preview->set_editable(false);
	change_preview->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	change_preview->set_custom_minimum_size(Size2(600, 340) * EDSCALE);
	apply_dialog->add_child(change_preview);
	apply_dialog->connect("confirmed", callable_mp(this, &HorrorAIAssistantEditorPlugin::_apply_changes));

	_build_settings_window();
	EditorDockManager::get_singleton()->add_dock(dock);
	dock->make_visible();
	_start_new_session();
}

void HorrorAIAssistantEditorPlugin::_build_settings_window() {
	settings_window = memnew(Window);
	settings_window->set_title(String::utf8("Azure OpenAI 接続設定"));
	settings_window->set_size(Size2i(560, 390));
	settings_window->set_exclusive(true);
	dock->add_child(settings_window);

	MarginContainer *margin = memnew(MarginContainer);
	margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	margin->add_theme_constant_override("margin_top", 16 * EDSCALE);
	margin->add_theme_constant_override("margin_bottom", 16 * EDSCALE);
	settings_window->add_child(margin);
	VBoxContainer *layout = memnew(VBoxContainer);
	layout->add_theme_constant_override("separation", 8 * EDSCALE);
	margin->add_child(layout);

	Label *description = memnew(Label);
	description->set_text(String::utf8("Azure OpenAIのリソースとデプロイ名を入力します。APIキーはこの編集セッション中だけ保持します。"));
	description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(description);

	auto add_setting_row = [&](const String &p_label, const String &p_placeholder, bool p_secret) -> LineEdit * {
		HBoxContainer *row = memnew(HBoxContainer);
		Label *label = memnew(Label);
		label->set_text(p_label);
		label->set_custom_minimum_size(Size2(135, 0) * EDSCALE);
		row->add_child(label);
		LineEdit *input = memnew(LineEdit);
		input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		input->set_placeholder(p_placeholder);
		if (p_secret) {
			input->set_secret(true);
		}
		row->add_child(input);
		layout->add_child(row);
		return input;
	};

	endpoint_input = add_setting_row(String::utf8("Endpoint"), "https://<resource>.openai.azure.com", false);
	deployment_input = add_setting_row(String::utf8("Deployment"), "deployment-name", false);
	api_version_input = add_setting_row(String::utf8("API version"), "2024-10-21", false);
	api_key_input = add_setting_row(String::utf8("API key"), String::utf8("AZURE_OPENAI_API_KEYでも設定できます"), true);

	HBoxContainer *buttons = memnew(HBoxContainer);
	buttons->set_alignment(BoxContainer::ALIGNMENT_END);
	Button *cancel_button = memnew(Button);
	cancel_button->set_text(String::utf8("キャンセル"));
	cancel_button->connect(SceneStringName(pressed), callable_mp(settings_window, &Window::hide));
	buttons->add_child(cancel_button);
	Button *save_button = memnew(Button);
	save_button->set_text(String::utf8("保存"));
	save_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_save_settings));
	buttons->add_child(save_button);
	layout->add_child(buttons);
}

void HorrorAIAssistantEditorPlugin::_start_new_session() {
	if (!staged_files.is_empty()) {
		status_label->set_text(String::utf8("先に未適用の変更案を確認してください。"));
		return;
	}
	messages.clear();
	Dictionary system_message;
	system_message["role"] = "system";
	system_message["content"] = String::utf8(
			"あなたは Mina Engine に組み込まれたゲーム制作エージェントです。Godot 4.7.2 のプロジェクトを調査し、ユーザーの指示に沿ってシーン、GDScript、音響の配置、演出イベントをまとめて制作します。日本語で応答してください。\n"
			"必ず利用可能なツールでプロジェクト構成と対象ファイルを調べてから編集案を作ってください。音声などのバイナリアセットを捏造せず、既存音声一覧から選んでください。必要な場合は .tscn と .gd を一緒に作成し、イベントのタイミングや音の再生、照明変化はスクリプトで実装してください。\n"
			"ファイル変更は stage_project_file だけで提案してください。既存ファイルを読む前に置き換えないでください。すべてのパスは res:// 以下で、テキスト形式のファイルだけを扱います。変更案はユーザーが一括確認してから適用されます。"
	);
	messages.push_back(system_message);
	if (transcript) {
		transcript->clear();
		transcript->add_text(String::utf8("Mina AI\n何を作りましょう？\n\n"));
	}
	if (session_title) {
		session_title->set_text("New Session");
	}
	if (status_label) {
		status_label->set_text(String::utf8("Azure OpenAI 接続待ち"));
	}
}
void HorrorAIAssistantEditorPlugin::_append_message(const String &p_role, const String &p_text) {
	if (!transcript) {
		return;
	}
	transcript->add_text(p_role == "user" ? String::utf8("あなた\n") : String::utf8("Mina AI\n"));
	transcript->add_text(p_text + "\n\n");
	transcript->scroll_to_line(transcript->get_line_count());
}

String HorrorAIAssistantEditorPlugin::_get_setting(const String &p_name, const String &p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (!settings || !settings->has_setting(p_name)) {
		return p_default;
	}
	return settings->get_setting(p_name);
}

void HorrorAIAssistantEditorPlugin::_show_settings() {
	endpoint_input->set_text(_get_setting("mina/ai/azure_endpoint", ""));
	deployment_input->set_text(_get_setting("mina/ai/azure_deployment", ""));
	api_version_input->set_text(_get_setting("mina/ai/azure_api_version", "2024-10-21"));
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	api_key_input->set_text(api_key);
	EditorInterface::get_singleton()->popup_dialog_centered(settings_window, Size2i(580, 420));
}

void HorrorAIAssistantEditorPlugin::_save_settings() {
	EditorSettings *settings = EditorSettings::get_singleton();
	settings->set_setting("mina/ai/azure_endpoint", endpoint_input->get_text().strip_edges());
	settings->set_setting("mina/ai/azure_deployment", deployment_input->get_text().strip_edges());
	settings->set_setting("mina/ai/azure_api_version", api_version_input->get_text().strip_edges());
	EditorSettings::save();
	api_key = api_key_input->get_text().strip_edges();
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	settings_window->hide();
	status_label->set_text(api_key.is_empty() ? String::utf8("APIキーを入力するか AZURE_OPENAI_API_KEY を設定してください。") : String::utf8("Azure OpenAIの設定を保存しました。"));
}

void HorrorAIAssistantEditorPlugin::_send_prompt() {
	if (busy) {
		return;
	}
	String prompt = prompt_input->get_text().strip_edges();
	if (prompt.is_empty()) {
		return;
	}
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	if (api_key.is_empty() || _get_setting("mina/ai/azure_endpoint", "").is_empty() || _get_setting("mina/ai/azure_deployment", "").is_empty()) {
		status_label->set_text(String::utf8("Azure OpenAIの接続設定が必要です。"));
		_show_settings();
		return;
	}
	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = prompt;
	messages.push_back(user_message);
	_append_message("user", prompt);
	prompt_input->clear();
	tool_round = 0;
	_set_busy(true, String::utf8("Azure OpenAIに接続しています…"));
	_request_next_turn();
}

void HorrorAIAssistantEditorPlugin::_request_next_turn() {
	String endpoint = _get_setting("mina/ai/azure_endpoint", "").strip_edges();
	while (endpoint.ends_with("/")) {
		endpoint = endpoint.substr(0, endpoint.length() - 1);
	}
	if (!endpoint.begins_with("https://")) {
		_set_busy(false, String::utf8("Endpointには https:// から始まるAzure OpenAI URLを入力してください。"));
		return;
	}
	String deployment = _get_setting("mina/ai/azure_deployment", "").strip_edges();
	String api_version = _get_setting("mina/ai/azure_api_version", "2024-10-21").strip_edges();
	if (deployment.is_empty() || api_version.is_empty()) {
		_set_busy(false, String::utf8("DeploymentとAPI versionを設定してください。"));
		return;
	}
	String url = endpoint + "/openai/deployments/" + deployment.uri_encode() + "/chat/completions?api-version=" + api_version.uri_encode();
	Dictionary request_body;
	request_body["messages"] = messages;
	request_body["tools"] = _get_tool_schemas();
	request_body["tool_choice"] = "auto";
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("api-key: " + api_key);
	Error error = http_request->request(url, headers, HTTPClient::METHOD_POST, JSON::stringify(request_body));
	if (error != OK) {
		_set_busy(false, vformat(String::utf8("Azure OpenAIへのリクエストを開始できませんでした (%s)。"), error));
	}
}

void HorrorAIAssistantEditorPlugin::_on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String response_text;
	if (!p_body.is_empty()) {
		response_text = String::utf8((const char *)p_body.ptr(), p_body.size());
	}
	JSON parser;
	Error parse_error = parser.parse(response_text);
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		String error_text;
		if (parse_error == OK && parser.get_data().get_type() == Variant::DICTIONARY) {
			Dictionary response = parser.get_data();
			Dictionary error = response.get("error", Dictionary());
			error_text = error.get("message", "");
		}
		if (error_text.is_empty()) {
			error_text = vformat("HTTP %d (result %d)", p_response_code, p_result);
		}
		_set_busy(false, String::utf8("Azure OpenAIエラー: ") + error_text);
		return;
	}
	if (parse_error != OK || parser.get_data().get_type() != Variant::DICTIONARY) {
		_set_busy(false, String::utf8("Azure OpenAIの応答をJSONとして読み取れませんでした。"));
		return;
	}
	Dictionary response = parser.get_data();
	Array choices = response.get("choices", Array());
	if (choices.is_empty()) {
		_set_busy(false, String::utf8("Azure OpenAIから応答がありませんでした。"));
		return;
	}
	Dictionary choice = choices[0];
	Dictionary assistant_message = choice.get("message", Dictionary());
	Array tool_calls = assistant_message.get("tool_calls", Array());
	if (!tool_calls.is_empty()) {
		messages.push_back(assistant_message);
		for (int i = 0; i < tool_calls.size(); i++) {
			Dictionary tool_call = tool_calls[i];
			Dictionary function = tool_call.get("function", Dictionary());
			String arguments_text = function.get("arguments", "{}");
			JSON arguments_parser;
			Dictionary arguments;
			if (arguments_parser.parse(arguments_text) == OK && arguments_parser.get_data().get_type() == Variant::DICTIONARY) {
				arguments = arguments_parser.get_data();
			}
			Dictionary result = _execute_tool(function.get("name", ""), arguments);
			Dictionary tool_message;
			tool_message["role"] = "tool";
			tool_message["tool_call_id"] = tool_call.get("id", "");
			tool_message["content"] = JSON::stringify(result);
			messages.push_back(tool_message);
		}
		tool_round++;
		if (tool_round > 6) {
			_set_busy(false, String::utf8("ツール実行回数の上限に達しました。指示を分けて続けてください。"));
			return;
		}
		_set_busy(true, String::utf8("プロジェクトを調査・編集しています…"));
		_request_next_turn();
		return;
	}
	String answer = assistant_message.get("content", "");
	if (answer.is_empty()) {
		answer = String::utf8("応答は返りましたが、本文が空でした。Deploymentのチャット対応を確認してください。 ");
	}
	messages.push_back(assistant_message);
	_append_message("assistant", answer);
	_set_busy(false, staged_files.is_empty() ? String::utf8("応答しました。") : String::utf8("変更案を確認して適用できます。"));
	_update_pending_changes();
}

void HorrorAIAssistantEditorPlugin::_set_busy(bool p_busy, const String &p_status) {
	busy = p_busy;
	if (send_button) {
		send_button->set_disabled(p_busy);
	}
	if (status_label && !p_status.is_empty()) {
		status_label->set_text(p_status);
	}
}

Array HorrorAIAssistantEditorPlugin::_get_tool_schemas() const {
	Array tools;
	Dictionary properties;
	Array required;

	properties["directory"] = _schema_string("調べるフォルダー。通常は res:// 。");
	tools.push_back(_function_tool("list_project_files", "プロジェクトのシーン、スクリプト、設定ファイルを一覧します。", properties, required));
	properties.clear();
	tools.push_back(_function_tool("list_audio_files", "プロジェクト内の既存音声ファイルを一覧します。", properties, required));

	properties.clear();
	properties["path"] = _schema_string("読み込むテキストファイルの res:// パス。");
	required.clear();
	required.push_back("path");
	tools.push_back(_function_tool("read_project_file", "プロジェクト内のシーン、スクリプト、リソース設定を読みます。", properties, required));

	properties.clear();
	required.clear();
	tools.push_back(_function_tool("inspect_current_scene", "現在エディターで開いているシーンのノード構成を調べます。", properties, required));

	properties.clear();
	properties["path"] = _schema_string("作成または更新する res:// 以下のテキストファイル。");
	properties["content"] = _schema_string("適用するファイル全体の内容。");
	properties["reason"] = _schema_string("このファイルを作成・更新する理由。");
	required.clear();
	required.push_back("path");
	required.push_back("content");
	required.push_back("reason");
	tools.push_back(_function_tool("stage_project_file", "ファイル全体の変更案をステージします。実際の書き込みはユーザーが一覧を確認してから行われます。既存ファイルは先にread_project_fileで読んでください。", properties, required));
	return tools;
}

Dictionary HorrorAIAssistantEditorPlugin::_execute_tool(const String &p_name, const Dictionary &p_arguments) {
	if (p_name == "list_project_files") {
		String directory = p_arguments.get("directory", "res://");
		if (directory.is_empty()) {
			directory = "res://";
	}
		return _list_project_files(directory, false);
	}
	if (p_name == "list_audio_files") {
		return _list_project_files("res://", true);
	}
	if (p_name == "read_project_file") {
		return _read_project_file(p_arguments.get("path", ""));
	}
	if (p_name == "inspect_current_scene") {
		return _inspect_current_scene();
	}
	if (p_name == "stage_project_file") {
		return _stage_project_file(p_arguments.get("path", ""), p_arguments.get("content", ""), p_arguments.get("reason", ""));
	}
	Dictionary error;
	error["ok"] = false;
	error["error"] = "Unknown tool: " + p_name;
	return error;
}

Dictionary HorrorAIAssistantEditorPlugin::_list_project_files(const String &p_directory, bool p_audio_only) const {
	Dictionary result;
	if (!_is_safe_project_path(p_directory, true)) {
		result["ok"] = false;
		result["error"] = "Directory must be inside res://.";
		return result;
	}
	Array files;
	int count = 0;
	_collect_project_files(p_directory, files, 160, p_audio_only, count);
	files.sort();
	result["ok"] = true;
	result["root"] = p_directory;
	result["files"] = files;
	result["truncated"] = count >= 160;
	return result;
}

void HorrorAIAssistantEditorPlugin::_collect_project_files(const String &p_directory, Array &r_files, int p_limit, bool p_audio_only, int &r_count) const {
	if (r_count >= p_limit) {
		return;
	}
	PackedStringArray names = DirAccess::get_files_at(p_directory);
	for (int i = 0; i < names.size() && r_count < p_limit; i++) {
		String file_name = names[i];
		String full_path = p_directory.ends_with("/") ? p_directory + file_name : p_directory + "/" + file_name;
		if (_is_ignored_project_path(full_path)) {
			continue;
		}
		if (p_audio_only) {
			String extension = file_name.get_extension().to_lower();
			if (extension != "wav" && extension != "ogg" && extension != "mp3" && extension != "flac" && extension != "aiff") {
				continue;
			}
		}
		r_files.push_back(full_path);
		r_count++;
	}
	if (p_audio_only || r_count >= p_limit) {
		return;
	}
	PackedStringArray directories = DirAccess::get_directories_at(p_directory);
	for (int i = 0; i < directories.size() && r_count < p_limit; i++) {
		String directory_name = directories[i];
		if (directory_name.begins_with(".")) {
			continue;
		}
		String full_path = p_directory.ends_with("/") ? p_directory + directory_name : p_directory + "/" + directory_name;
		if (_is_ignored_project_path(full_path)) {
			continue;
		}
		_collect_project_files(full_path, r_files, p_limit, false, r_count);
	}
}

Dictionary HorrorAIAssistantEditorPlugin::_read_project_file(const String &p_path) const {
	Dictionary result;
	if (!_is_safe_project_path(p_path, false) || !_is_allowed_text_path(p_path)) {
		result["ok"] = false;
		result["error"] = "Only allowed text files inside res:// can be read.";
		return result;
	}
	if (staged_files.has(p_path)) {
		Dictionary staged = staged_files[p_path];
		result["ok"] = true;
		result["path"] = p_path;
		result["content"] = staged.get("content", "");
		result["staged"] = true;
		return result;
	}
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
	if (file.is_null()) {
		result["ok"] = false;
		result["error"] = vformat("Could not read %s (error %d).", p_path, error);
		return result;
	}
	if (file->get_length() > 131072) {
		result["ok"] = false;
		result["error"] = "File is larger than the 128 KB context limit.";
		return result;
	}
	result["ok"] = true;
	result["path"] = p_path;
	result["content"] = file->get_as_text();
	result["staged"] = false;
	return result;
}

Dictionary HorrorAIAssistantEditorPlugin::_stage_project_file(const String &p_path, const String &p_content, const String &p_reason) {
	Dictionary result;
	if (!_is_safe_project_path(p_path, false) || !_is_allowed_text_path(p_path)) {
		result["ok"] = false;
		result["error"] = "Only allowed text files inside res:// can be changed.";
		return result;
	}
	if (p_content.length() > 524288) {
		result["ok"] = false;
		result["error"] = "A staged file cannot exceed 512 KB of text.";
		return result;
	}
	int total_characters = p_content.length();
	Array staged_paths = staged_files.keys();
	for (int i = 0; i < staged_paths.size(); i++) {
		if (staged_paths[i] != p_path) {
			Dictionary staged = staged_files[staged_paths[i]];
			total_characters += String(staged.get("content", "")).length();
		}
	}
	if (total_characters > 2097152) {
		result["ok"] = false;
		result["error"] = "The total staged text cannot exceed 2 MB.";
		return result;
	}

	Dictionary entry = staged_files.get(p_path, Dictionary());
	if (entry.is_empty()) {
		bool exists = FileAccess::exists(p_path);
		String original;
		if (exists) {
			Error error = OK;
			Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &error);
			if (file.is_null() || file->get_length() > 524288) {
				result["ok"] = false;
				result["error"] = file.is_null() ? vformat("Could not read existing file (error %d).", error) : "Existing file is larger than the 512 KB edit limit.";
				return result;
			}
			original = file->get_as_text();
		}
		entry["exists"] = exists;
		entry["original"] = original;
	}
	int review_characters = p_content.length() + String(entry.get("original", "")).length();
	for (int i = 0; i < staged_paths.size(); i++) {
		if (staged_paths[i] != p_path) {
			Dictionary staged = staged_files[staged_paths[i]];
			review_characters += String(staged.get("content", "")).length() + String(staged.get("original", "")).length();
		}
	}
	if (review_characters > 4194304) {
		result["ok"] = false;
		result["error"] = "The total review preview cannot exceed 4 MB of text.";
		return result;
	}
	entry["content"] = p_content;
	entry["reason"] = p_reason;
	staged_files[p_path] = entry;
	_update_pending_changes();
	result["ok"] = true;
	result["path"] = p_path;
	result["reason"] = p_reason;
	result["characters"] = p_content.length();
	result["message"] = "Change staged for user review; it has not been written yet.";
	return result;
}

Dictionary HorrorAIAssistantEditorPlugin::_inspect_current_scene() const {
	Dictionary result;
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	Node *root = editor_interface ? editor_interface->get_edited_scene_root() : nullptr;
	if (!root) {
		result["ok"] = false;
		result["error"] = "No scene is currently open.";
		return result;
	}
	Array nodes;
	_collect_scene_nodes(root, ".", nodes, 200);
	result["ok"] = true;
	result["scene_path"] = root->get_scene_file_path();
	result["root_type"] = root->get_class();
	result["nodes"] = nodes;
	result["truncated"] = nodes.size() >= 200;
	return result;
}

void HorrorAIAssistantEditorPlugin::_collect_scene_nodes(Node *p_node, const String &p_path, Array &r_nodes, int p_limit) const {
	if (!p_node || r_nodes.size() >= p_limit) {
		return;
	}
	Dictionary node_info;
	node_info["path"] = p_path;
	node_info["name"] = p_node->get_name();
	node_info["type"] = p_node->get_class();
	Ref<Script> script = p_node->get_script();
	if (script.is_valid()) {
		node_info["script"] = script->get_path();
	}
	r_nodes.push_back(node_info);
	for (int i = 0; i < p_node->get_child_count() && r_nodes.size() < p_limit; i++) {
		Node *child = p_node->get_child(i);
		String child_path = p_path == "." ? String(child->get_name()) : p_path + "/" + String(child->get_name());
		_collect_scene_nodes(child, child_path, r_nodes, p_limit);
	}
}

bool HorrorAIAssistantEditorPlugin::_is_safe_project_path(const String &p_path, bool p_allow_directory) const {
	if (!p_path.begins_with("res://") || p_path.contains("\\") || p_path.contains(":")) {
		return false;
	}
	String relative = p_path.trim_prefix("res://");
	if (relative.is_empty()) {
		return p_allow_directory;
	}
	PackedStringArray segments = relative.split("/");
	for (int i = 0; i < segments.size(); i++) {
		if (segments[i].is_empty() || segments[i] == "." || segments[i] == "..") {
			return false;
		}
	}
	return !_is_ignored_project_path(p_path);
}

bool HorrorAIAssistantEditorPlugin::_is_allowed_text_path(const String &p_path) const {
	if (!_is_safe_project_path(p_path, false) || p_path.ends_with("/")) {
		return false;
	}
	String extension = p_path.get_extension().to_lower();
	return extension == "gd" || extension == "tscn" || extension == "tres" || extension == "godot" || extension == "cfg" || extension == "json" || extension == "md" || extension == "txt" || extension == "shader" || extension == "gdshader" || extension == "gdshaderinc" || extension == "csv" || extension == "translation";
}

bool HorrorAIAssistantEditorPlugin::_is_ignored_project_path(const String &p_path) const {
	String lower = p_path.to_lower();
	PackedStringArray segments = lower.trim_prefix("res://").split("/");
	for (int i = 0; i < segments.size(); i++) {
		if (segments[i] == ".git" || segments[i] == ".godot" || segments[i].begins_with(".env") || segments[i] == "secrets" || segments[i] == "credentials" || segments[i] == "private") {
			return true;
		}
	}
	return lower.ends_with(".pem") || lower.ends_with(".key") || lower.ends_with(".p12") || lower.ends_with(".pfx") || lower.ends_with(".env") || lower.ends_with(".keystore");
}

void HorrorAIAssistantEditorPlugin::_update_pending_changes() {
	if (!apply_button) {
		return;
	}
	apply_button->set_visible(!staged_files.is_empty());
	apply_button->set_text(vformat(String::utf8("変更案を確認して適用 (%dファイル)"), staged_files.size()));
}

void HorrorAIAssistantEditorPlugin::_confirm_apply_changes() {
	if (staged_files.is_empty()) {
		return;
	}
	String summary = String::utf8("AIが提案したファイル内容です。確認してから適用してください。既存ファイルはバックアップされます。\n");
	Array paths = staged_files.keys();
	paths.sort();
	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary entry = staged_files[path];
		summary += String(entry.get("exists", false) ? "• 更新: " : "• 作成: ") + path + "\n";
		String reason = entry.get("reason", "");
		if (!reason.is_empty()) {
			summary += "  " + reason + "\n";
		}
		summary += "\n";
		if (bool(entry.get("exists", false))) {
			summary += String::utf8("--- 変更前 ---\n") + String(entry.get("original", "")) + "\n";
		}
		summary += String::utf8("--- 変更後 ---\n") + String(entry.get("content", "")) + "\n\n";
	}
	change_preview->set_text(summary);
	EditorInterface::get_singleton()->popup_dialog_centered(apply_dialog, Size2i(620, 440));
}

void HorrorAIAssistantEditorPlugin::_apply_changes() {
	if (staged_files.is_empty()) {
		return;
	}
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	PackedStringArray unsaved_scenes = editor_interface->get_unsaved_scenes();
	Array paths = staged_files.keys();
	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary entry = staged_files[path];
		bool file_exists = FileAccess::exists(path);
		if (file_exists != bool(entry.get("exists", false)) || (!file_exists && DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(path)))) {
			status_label->set_text(vformat(String::utf8("変更案を作成した後にファイル状態が変わりました。再度AIに依頼してください: %s"), path));
			return;
		}
		if (file_exists) {
			Error read_error = OK;
			Ref<FileAccess> current_file = FileAccess::open(path, FileAccess::READ, &read_error);
			if (current_file.is_null() || current_file->get_length() > 524288 || current_file->get_as_text() != String(entry.get("original", ""))) {
				status_label->set_text(vformat(String::utf8("変更案を作成した後にファイル内容が変わりました。再度AIに依頼してください: %s"), path));
				return;
			}
		}
		if (!path.ends_with(".tscn")) {
			continue;
		}
		if (unsaved_scenes.has(path) || unsaved_scenes.has(ProjectSettings::get_singleton()->globalize_path(path))) {
			status_label->set_text(String::utf8("未保存のシーンを含む変更案です。先にシーンを保存してから、もう一度適用してください。"));
			return;
		}
	}

	String backup_root = OS::get_singleton()->get_user_data_dir().path_join("mina_ai_backups").path_join(String::num_uint64(OS::get_singleton()->get_ticks_msec()));
	Error error = DirAccess::make_dir_recursive_absolute(backup_root);
	if (error != OK) {
		status_label->set_text(vformat(String::utf8("バックアップ用フォルダーを作れませんでした (%d)。"), error));
		return;
	}

	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary entry = staged_files[path];
		if (!bool(entry.get("exists", false))) {
			continue;
		}
		String backup_path = backup_root.path_join(path.trim_prefix("res://"));
		error = DirAccess::make_dir_recursive_absolute(backup_path.get_base_dir());
		if (error != OK) {
			status_label->set_text(vformat(String::utf8("バックアップ先を作れませんでした: %s"), backup_path));
			return;
		}
		Ref<FileAccess> backup = FileAccess::open(backup_path, FileAccess::WRITE, &error);
		if (backup.is_null() || !backup->store_string(entry.get("original", ""))) {
			status_label->set_text(vformat(String::utf8("既存ファイルのバックアップに失敗しました: %s"), path));
			return;
		}
	}

	Array written_paths;
	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary entry = staged_files[path];
		String absolute_directory = ProjectSettings::get_singleton()->globalize_path(path.get_base_dir());
		error = DirAccess::make_dir_recursive_absolute(absolute_directory);
		if (error != OK) {
			break;
		}
		Ref<FileAccess> output = FileAccess::open(path, FileAccess::WRITE, &error);
		written_paths.push_back(path);
		if (output.is_null() || !output->store_string(entry.get("content", ""))) {
			break;
		}
	}
	if (written_paths.size() != paths.size()) {
		for (int i = 0; i < written_paths.size(); i++) {
			String path = written_paths[i];
			Dictionary entry = staged_files[path];
			if (bool(entry.get("exists", false))) {
				Ref<FileAccess> restore = FileAccess::open(path, FileAccess::WRITE);
				if (restore.is_valid()) {
					restore->store_string(entry.get("original", ""));
				}
			} else if (FileAccess::exists(path)) {
				DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(path));
			}
		}
		status_label->set_text(vformat(String::utf8("ファイル適用に失敗したため、元の内容へ戻しました。エラー: %d"), error));
		return;
	}

	String active_scene_path;
	Node *active_scene = editor_interface->get_edited_scene_root();
	if (active_scene) {
		active_scene_path = active_scene->get_scene_file_path();
	}
	EditorFileSystem *filesystem = editor_interface->get_resource_filesystem();
	if (filesystem) {
		filesystem->scan();
	}
	if (!active_scene_path.is_empty() && staged_files.has(active_scene_path)) {
		editor_interface->reload_scene_from_path(active_scene_path);
	}
	int changed_count = staged_files.size();
	staged_files.clear();
	_update_pending_changes();
	status_label->set_text(vformat(String::utf8("%dファイルを適用しました。バックアップ: %s"), changed_count, backup_root));
	_append_message("assistant", vformat(String::utf8("変更を適用しました。バックアップは `%s` に保存しました。"), backup_root));
}

void HorrorAIAssistantEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_panel();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (dock) {
				EditorDockManager::get_singleton()->remove_dock(dock);
				memdelete(dock);
				dock = nullptr;
			}
		} break;
	}
}

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

#include "core/core_bind.h"
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
#include "editor/file_system/editor_paths.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

constexpr int MAX_IMAGE_ATTACHMENTS = 4;
constexpr int64_t MAX_IMAGE_BYTES = 5 * 1024 * 1024;
constexpr int64_t MAX_TOTAL_IMAGE_BYTES = 10 * 1024 * 1024;

bool _is_supported_image_extension(const String &p_extension) {
	return p_extension == "png" || p_extension == "jpg" || p_extension == "jpeg" || p_extension == "webp";
}

String _get_image_mime_type(const String &p_extension) {
	if (p_extension == "png") {
		return "image/png";
	}
	if (p_extension == "webp") {
		return "image/webp";
	}
	return "image/jpeg";
}

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

	Dictionary tool;
	tool["type"] = "function";
	tool["name"] = p_name;
	tool["description"] = p_description;
	tool["parameters"] = parameters;
	return tool;
}

String _get_saved_settings_path() {
	OS *os = OS::get_singleton();
	String config_dir = os->get_config_path();
	if (config_dir.is_empty()) {
		config_dir = os->get_user_data_dir();
	}
	return config_dir.path_join("Mina Engine").path_join("mina_ai_settings.cfg");
}

Error _save_saved_settings(const Ref<ConfigFile> &p_settings) {
	String path = _get_saved_settings_path();
	Error dir_error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
		return dir_error;
	}
	return p_settings->save(path);
}

String _get_session_storage_path() {
	return OS::get_singleton()->get_user_data_dir().path_join("mina_ai_sessions.cfg");
}

Error _save_session_storage(const Ref<ConfigFile> &p_session_storage) {
	String path = _get_session_storage_path();
	Error dir_error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
		return dir_error;
	}
	return p_session_storage->save(path);
}

String _encrypt_api_key(const String &p_api_key) {
#ifdef WINDOWS_ENABLED
	CharString key_utf8 = p_api_key.utf8();
	DATA_BLOB input = { (DWORD)key_utf8.length(), (BYTE *)key_utf8.get_data() };
	DATA_BLOB encrypted = {};
	if (!CryptProtectData(&input, L"Mina Engine Azure OpenAI API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
		return String();
	}
	String result = String::hex_encode_buffer(encrypted.pbData, encrypted.cbData);
	LocalFree(encrypted.pbData);
	return result;
#else
	// shortcut: other platforms must provide the key through the environment variable.
	return String();
#endif
}

String _decrypt_api_key(const String &p_encrypted_key) {
#ifdef WINDOWS_ENABLED
	Vector<uint8_t> encrypted_data = p_encrypted_key.hex_decode();
	if (encrypted_data.is_empty()) {
		return String();
	}
	DATA_BLOB input = { (DWORD)encrypted_data.size(), encrypted_data.ptrw() };
	DATA_BLOB decrypted = {};
	if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted)) {
		return String();
	}
	String result = String::utf8((const char *)decrypted.pbData, decrypted.cbData);
	SecureZeroMemory(decrypted.pbData, decrypted.cbData);
	LocalFree(decrypted.pbData);
	return result;
#else
	return String();
#endif
}

} // namespace

void HorrorAIAssistantEditorPlugin::_build_panel() {
	_load_settings();
	_load_session_history();
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
	session_title->set_text(String::utf8("新しいセッション"));
	session_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header->add_child(session_title);
	history_button = memnew(Button);
	history_button->set_text(String::utf8("履歴"));
	history_button->set_tooltip_text(String::utf8("過去のチャットを開く"));
	history_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_show_session_history));
	header->add_child(history_button);
	Button *new_session_button = memnew(Button);
	new_session_button->set_text(String::utf8("新規"));
	new_session_button->set_tooltip_text(String::utf8("新しいセッション"));
	new_session_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_start_new_session));
	header->add_child(new_session_button);
	Button *settings_button = memnew(Button);
	settings_button->set_text(String::utf8("設定"));
	settings_button->set_tooltip_text(String::utf8("Azure OpenAI設定"));
	settings_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_show_settings));
	header->add_child(settings_button);
	layout->add_child(header);
	layout->add_child(memnew(HSeparator));

	transcript_scroll = memnew(ScrollContainer);
	transcript_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	transcript_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	layout->add_child(transcript_scroll);
	message_list = memnew(VBoxContainer);
	message_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->set_custom_minimum_size(Size2(0, 220) * EDSCALE);
	message_list->add_theme_constant_override("separation", 18 * EDSCALE);
	transcript_scroll->add_child(message_list);

	activity_button = memnew(Button);
	activity_button->set_text(String::utf8("作業内容を表示"));
	activity_button->set_visible(false);
	activity_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_toggle_activity_details));
	layout->add_child(activity_button);
	activity_details = memnew(TextEdit);
	activity_details->set_editable(false);
	activity_details->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	activity_details->set_custom_minimum_size(Size2(0, 150) * EDSCALE);
	activity_details->set_visible(false);
	layout->add_child(activity_details);

	status_label = memnew(Label);
	status_label->set_text(String::utf8("Azure OpenAI未設定"));
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_visible(false);
	layout->add_child(status_label);

	prompt_input = memnew(TextEdit);
	prompt_input->set_placeholder(String::utf8("作りたいゲームや怖い演出を説明してください…"));
	prompt_input->set_custom_minimum_size(Size2(0, 88) * EDSCALE);
	prompt_input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	layout->add_child(prompt_input);
	attachment_row = memnew(HBoxContainer);
	attachment_label = memnew(Label);
	attachment_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	attachment_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	attachment_row->add_child(attachment_label);
	clear_attachments_button = memnew(Button);
	clear_attachments_button->set_text(String::utf8("×"));
	clear_attachments_button->set_tooltip_text(String::utf8("画像添付を解除"));
	clear_attachments_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_clear_image_attachments));
	attachment_row->add_child(clear_attachments_button);
	attachment_row->set_visible(false);
	layout->add_child(attachment_row);

	HBoxContainer *input_footer = memnew(HBoxContainer);
	Button *attach_image_button = memnew(Button);
	attach_image_button->set_text(String::utf8("＋画像"));
	attach_image_button->set_tooltip_text(String::utf8("PNG/JPEG/WebPを添付できます。画像はAzure OpenAIに送信されます。最大4枚、1枚5 MBまで。"));
	attach_image_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_open_image_dialog));
	input_footer->add_child(attach_image_button);
	Label *privacy_note = memnew(Label);
	privacy_note->set_text(String::utf8("変更は自動適用・バックアップあり"));
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
	image_file_dialog = memnew(FileDialog);
	image_file_dialog->set_title(String::utf8("添付する画像を選択"));
	image_file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILES);
	image_file_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	image_file_dialog->add_filter("*.png, *.jpg, *.jpeg, *.webp", String::utf8("画像ファイル"));
	image_file_dialog->connect("files_selected", callable_mp(this, &HorrorAIAssistantEditorPlugin::_on_images_selected));
	dock->add_child(image_file_dialog);

	_build_settings_window();
	_build_session_history_window();
	EditorDockManager::get_singleton()->add_dock(dock);
	dock->make_visible();
	_start_new_session();
}

void HorrorAIAssistantEditorPlugin::_build_settings_window() {
	settings_window = memnew(Window);
	settings_window->set_visible(false);
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
#ifdef WINDOWS_ENABLED
	description->set_text(String::utf8("API versionは自動設定です。EndpointとDeploymentを入力してください。APIキーはWindowsユーザーアカウントで暗号化して保存します。"));
#else
	description->set_text(String::utf8("API versionは自動設定です。EndpointとDeploymentを入力してください。APIキーは AZURE_OPENAI_API_KEY 環境変数で設定してください。"));
#endif
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

void HorrorAIAssistantEditorPlugin::_build_session_history_window() {
	session_history_window = memnew(Window);
	session_history_window->set_visible(false);
	session_history_window->set_title(String::utf8("チャット履歴"));
	session_history_window->set_size(Size2i(420, 460));
	session_history_window->set_exclusive(true);
	dock->add_child(session_history_window);

	VBoxContainer *layout = memnew(VBoxContainer);
	layout->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	layout->add_theme_constant_override("separation", 8 * EDSCALE);
	session_history_window->add_child(layout);
	session_history_list = memnew(ItemList);
	session_history_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	session_history_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	session_history_list->connect("item_activated", callable_mp(this, &HorrorAIAssistantEditorPlugin::_load_session_from_history_item));
	layout->add_child(session_history_list);

	HBoxContainer *buttons = memnew(HBoxContainer);
	buttons->set_alignment(BoxContainer::ALIGNMENT_END);
	Button *close_button = memnew(Button);
	close_button->set_text(String::utf8("閉じる"));
	close_button->connect(SceneStringName(pressed), callable_mp(session_history_window, &Window::hide));
	buttons->add_child(close_button);
	Button *open_button = memnew(Button);
	open_button->set_text(String::utf8("開く"));
	open_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_open_selected_session));
	buttons->add_child(open_button);
	layout->add_child(buttons);
	_refresh_session_history_list();
}

void HorrorAIAssistantEditorPlugin::_load_session_history() {
	session_storage.instantiate();
	Error load_error = session_storage->load(_get_session_storage_path());
	if (load_error != OK && load_error != ERR_FILE_NOT_FOUND) {
		WARN_PRINT(vformat("Could not load Mina AI chat history (error %d).", load_error));
	}
	session_ids = session_storage->get_value("history", "ids", Array());
}

void HorrorAIAssistantEditorPlugin::_refresh_session_history_list() {
	if (!session_history_list || session_storage.is_null()) {
		return;
	}
	session_history_list->clear();
	session_ids = session_storage->get_value("history", "ids", Array());
	for (int i = 0; i < session_ids.size(); i++) {
		String session_id = session_ids[i];
		String section = "thread_" + session_id;
		if (!session_storage->has_section(section)) {
			continue;
		}
		String title = session_storage->get_value(section, "title", String::utf8("チャット"));
		int item = session_history_list->add_item(title);
		session_history_list->set_item_metadata(item, session_id);
	}
}

void HorrorAIAssistantEditorPlugin::_show_session_history() {
	if (!session_history_window) {
		return;
	}
	_refresh_session_history_list();
	session_history_window->popup_centered_clamped(Size2i(420, 460));
}

void HorrorAIAssistantEditorPlugin::_open_selected_session() {
	if (!session_history_list || !session_history_window) {
		return;
	}
	Vector<int> selected = session_history_list->get_selected_items();
	if (selected.is_empty()) {
		return;
	}
	_load_session(String(session_history_list->get_item_metadata(selected[0])));
}

void HorrorAIAssistantEditorPlugin::_load_session_from_history_item(int p_index) {
	if (!session_history_list) {
		return;
	}
	_load_session(String(session_history_list->get_item_metadata(p_index)));
}

void HorrorAIAssistantEditorPlugin::_load_session(const String &p_session_id) {
	if (busy || session_storage.is_null() || p_session_id.is_empty()) {
		return;
	}
	String section = "thread_" + p_session_id;
	if (!session_storage->has_section(section)) {
		return;
	}
	Array saved_messages = session_storage->get_value(section, "messages", Array());
	_start_new_session();
	current_session_id = p_session_id;
	conversation_messages = saved_messages;
	if (session_title) {
		session_title->set_text(session_storage->get_value(section, "title", String::utf8("チャット")));
	}
	if (message_list) {
		for (int i = 0; i < message_list->get_child_count(); i++) {
			message_list->get_child(i)->queue_free();
		}
	}
	responses_input_items.clear();
	restoring_session = true;
	for (int i = 0; i < saved_messages.size(); i++) {
		Dictionary saved_message = saved_messages[i];
		String role = saved_message.get("role", "assistant");
		String message_text = saved_message.get("text", "");
		Array images = saved_message.get("images", Array());
		_append_message(role, message_text, images);

		Dictionary context_message;
		context_message["type"] = "message";
		context_message["role"] = role;
		Array content;
		Dictionary text_part;
		text_part["type"] = role == "user" ? "input_text" : "output_text";
		String context_text = message_text;
		if (role == "user" && !images.is_empty()) {
			context_text += context_text.is_empty() ? String() : "\n";
			context_text += String::utf8("以前添付した画像: ");
			for (int j = 0; j < images.size(); j++) {
				if (j > 0) {
					context_text += ", ";
				}
				context_text += String(images[j]).get_file();
			}
		}
		text_part["text"] = context_text;
		content.push_back(text_part);
		context_message["content"] = content;
		responses_input_items.push_back(context_message);
	}
	restoring_session = false;
	if (session_history_window) {
		session_history_window->hide();
	}
}

void HorrorAIAssistantEditorPlugin::_save_current_session() {
	if (current_session_id.is_empty() || conversation_messages.is_empty() || session_storage.is_null()) {
		return;
	}
	String section = "thread_" + current_session_id;
	session_storage->set_value(section, "title", session_title ? session_title->get_text() : String::utf8("チャット"));
	session_storage->set_value(section, "messages", conversation_messages);
	if (!session_ids.has(current_session_id)) {
		session_ids.push_front(current_session_id);
	}
	session_storage->set_value("history", "ids", session_ids);
	Error save_error = _save_session_storage(session_storage);
	if (save_error != OK) {
		WARN_PRINT(vformat("Could not save Mina AI chat history (error %d).", save_error));
	}
	_refresh_session_history_list();
}

void HorrorAIAssistantEditorPlugin::_open_image_dialog() {
	if (image_file_dialog) {
		image_file_dialog->popup_centered_clamped(Size2i(840, 620));
	}
}

void HorrorAIAssistantEditorPlugin::_on_images_selected(const PackedStringArray &p_paths) {
	int64_t total_bytes = 0;
	for (int i = 0; i < pending_image_paths.size(); i++) {
		Ref<FileAccess> file = FileAccess::open(pending_image_paths[i], FileAccess::READ);
		if (file.is_valid()) {
			total_bytes += file->get_length();
		}
	}
	String error_message;
	for (int i = 0; i < p_paths.size(); i++) {
		String path = p_paths[i];
		if (pending_image_paths.has(path)) {
			continue;
		}
		if (pending_image_paths.size() >= MAX_IMAGE_ATTACHMENTS) {
			error_message = String::utf8("1回に添付できる画像は最大4枚です。");
			break;
		}
		String extension = path.get_extension().to_lower();
		if (!_is_supported_image_extension(extension)) {
			error_message = String::utf8("PNG、JPEG、WebP画像を選択してください。");
			continue;
		}
		Error error = OK;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ, &error);
		if (file.is_null()) {
			error_message = vformat(String::utf8("画像を読み込めません: %s"), path.get_file());
			continue;
		}
		int64_t image_bytes = file->get_length();
		if (image_bytes <= 0 || image_bytes > MAX_IMAGE_BYTES || total_bytes + image_bytes > MAX_TOTAL_IMAGE_BYTES) {
			error_message = String::utf8("画像は1枚5 MB以下、合計10 MB以下にしてください。");
			continue;
		}
		pending_image_paths.push_back(path);
		total_bytes += image_bytes;
	}
	_refresh_image_attachments();
	if (!error_message.is_empty()) {
		_set_busy(false, error_message);
	}
}

void HorrorAIAssistantEditorPlugin::_clear_image_attachments() {
	pending_image_paths.clear();
	_refresh_image_attachments();
}

void HorrorAIAssistantEditorPlugin::_refresh_image_attachments() {
	if (!attachment_label) {
		return;
	}
	bool has_images = !pending_image_paths.is_empty();
	if (attachment_row) {
		attachment_row->set_visible(has_images);
	}
	if (clear_attachments_button) {
		clear_attachments_button->set_visible(has_images);
	}
	if (!has_images) {
		attachment_label->set_text(String());
		return;
	}
	String label = String::utf8("添付画像: ");
	for (int i = 0; i < pending_image_paths.size(); i++) {
		if (i > 0) {
			label += ", ";
		}
		label += String(pending_image_paths[i]).get_file();
	}
	attachment_label->set_text(label);
}

void HorrorAIAssistantEditorPlugin::_load_settings() {
	saved_settings.instantiate();
	String settings_path = _get_saved_settings_path();
	Error load_error = saved_settings->load(settings_path);
	bool migrated_from_file = false;
	if (load_error == ERR_FILE_NOT_FOUND) {
		EditorPaths *paths = EditorPaths::get_singleton();
		OS *os = OS::get_singleton();
		String legacy_path = (paths ? paths->get_config_dir() : os->get_user_data_dir()).path_join("mina_ai_settings.cfg");
		if (legacy_path != settings_path && FileAccess::exists(legacy_path)) {
			load_error = saved_settings->load(legacy_path);
			migrated_from_file = load_error == OK;
		}
		String previous_global_path = os->get_config_path().path_join(os->get_godot_dir_name()).path_join("mina_ai_settings.cfg");
		if (load_error == ERR_FILE_NOT_FOUND && previous_global_path != settings_path && previous_global_path != legacy_path && FileAccess::exists(previous_global_path)) {
			load_error = saved_settings->load(previous_global_path);
			migrated_from_file = load_error == OK;
		}
	}
	bool migrated = false;
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	static const char *setting_names[] = { "azure_endpoint", "azure_deployment" };
	for (int i = 0; i < 2; i++) {
		String key = setting_names[i];
		String setting_name = "mina/ai/" + key;
		if (!saved_settings->has_section_key("mina/ai", key) && editor_settings && editor_settings->has_setting(setting_name)) {
			saved_settings->set_value("mina/ai", key, editor_settings->get_setting(setting_name));
			migrated = true;
		}
	}
	if (migrated_from_file || migrated || load_error == ERR_FILE_NOT_FOUND) {
		Error save_error = _save_saved_settings(saved_settings);
		if (save_error != OK) {
			WARN_PRINT(vformat("Could not save Mina AI settings (error %d).", save_error));
		}
	}
	api_key = _decrypt_api_key(saved_settings->get_value("mina/ai", "azure_api_key_dpapi", ""));
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
}

void HorrorAIAssistantEditorPlugin::_start_new_session() {
	if (busy) {
		return;
	}
	if (!staged_files.is_empty()) {
		_discard_staged_changes();
	}
	responses_input_items.clear();
	conversation_messages.clear();
	current_session_id.clear();
	_clear_image_attachments();
	activity_entries.clear();
	if (activity_button) {
		activity_button->set_visible(false);
	}
	if (activity_details) {
		activity_details->clear();
		activity_details->set_visible(false);
	}
	system_instructions = String::utf8(
			"あなたは Mina Engine に組み込まれたゲーム制作エージェントです。Godot 4.7.2 のプロジェクトを調査し、ユーザーの指示に沿ってシーン、GDScript、音響の配置、演出イベントをまとめて制作します。日本語で応答してください。\n"
			"必ず利用可能なツールでプロジェクト構成と対象ファイルを調べてから編集案を作ってください。音声などのバイナリアセットを捏造せず、既存音声一覧から選んでください。必要な場合は .tscn と .gd を一緒に作成し、イベントのタイミングや音の再生、照明変化はスクリプトで実装してください。\n"
			"ファイル変更は stage_project_file だけで提案してください。既存ファイルを読む前に置き換えないでください。すべてのパスは res:// 以下で、テキスト形式のファイルだけを扱います。AIの回答が完了した後、変更案はバックアップを作成して自動適用されます。"
	);
	if (message_list) {
		for (int i = 0; i < message_list->get_child_count(); i++) {
			message_list->get_child(i)->queue_free();
		}
		_append_message("assistant", String::utf8("何を作りましょう？"));
	}
	if (session_title) {
		session_title->set_text(String::utf8("新しいセッション"));
	}
	if (status_label) {
		status_label->set_text(String::utf8("Azure OpenAI 接続待ち"));
		status_label->set_visible(false);
	}
}

void HorrorAIAssistantEditorPlugin::_append_message(const String &p_role, const String &p_text, const Array &p_image_paths) {
	if (!message_list) {
		return;
	}
	String display_text = p_text;
	int line_count = p_text.count("\n") + 1;
	if (p_role == "user" && (p_text.length() > 1000 || line_count > 20)) {
		display_text = vformat(String::utf8("長文を送信しました（%d行・%d文字）。全文はAIに送信済みです。"), line_count, p_text.length());
	}
	bool is_user = p_role == "user";
	String copy_text = p_text;
	if (is_user && display_text.is_empty() && !p_image_paths.is_empty()) {
		display_text = String::utf8("画像を送信しました。");
	}
	for (int i = 0; is_user && i < p_image_paths.size(); i++) {
		display_text += (i == 0 ? "\n\n" : "\n") + String::utf8("画像: ") + String(p_image_paths[i]).get_file();
		copy_text += String::utf8(i == 0 ? "\n\n" : "\n") + String::utf8("画像: ") + String(p_image_paths[i]).get_file();
	}
	if (is_user && copy_text.is_empty() && !p_image_paths.is_empty()) {
		copy_text = display_text;
	}
	VBoxContainer *message = memnew(VBoxContainer);
	message->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message->add_theme_constant_override("separation", 3 * EDSCALE);
	message_list->add_child(message);

	HBoxContainer *message_row = memnew(HBoxContainer);
	message_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message->add_child(message_row);

	RichTextLabel *body = memnew(RichTextLabel);
	body->set_use_bbcode(false);
	body->set_fit_content(true);
	body->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	body->set_selection_enabled(true);
	body->set_text(display_text);
	if (is_user) {
		Control *spacer = memnew(Control);
		spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		message_row->add_child(spacer);

		real_t available_width = transcript_scroll ? transcript_scroll->get_size().x : 0;
		if (available_width <= 0 && dock) {
			available_width = dock->get_custom_minimum_size().x;
		}
		real_t max_bubble_width = MAX(140 * EDSCALE, available_width * 0.78);
		int longest_line = 0;
		PackedStringArray lines = display_text.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			longest_line = MAX(longest_line, lines[i].length());
		}
		real_t bubble_width = MIN(max_bubble_width, MAX(72 * EDSCALE, longest_line * 12 * EDSCALE + 28 * EDSCALE));

		PanelContainer *bubble = memnew(PanelContainer);
		bubble->set_h_size_flags(Control::SIZE_SHRINK_END);
		bubble->set_custom_minimum_size(Size2(bubble_width, 0));
		Ref<StyleBoxFlat> bubble_style;
		bubble_style.instantiate();
		Ref<Theme> editor_theme = EditorInterface::get_singleton()->get_editor_theme();
		if (editor_theme.is_valid()) {
			bubble_style->set_bg_color(editor_theme->get_color("dark_color_2", "Editor"));
		}
		bubble_style->set_corner_radius_all(16 * EDSCALE);
		bubble_style->set_content_margin_all(12 * EDSCALE);
		bubble->add_theme_style_override("panel", bubble_style);
		message_row->add_child(bubble);
		body->set_custom_minimum_size(Size2(MAX(48 * EDSCALE, bubble_width - 24 * EDSCALE), 0));
		bubble->add_child(body);
	} else {
		body->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		message_row->add_child(body);
	}

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	actions->set_alignment(is_user ? BoxContainer::ALIGNMENT_END : BoxContainer::ALIGNMENT_BEGIN);
	message->add_child(actions);
	Ref<Theme> editor_theme = EditorInterface::get_singleton()->get_editor_theme();
	Button *copy_button = memnew(Button);
	copy_button->set_flat(true);
	copy_button->set_tooltip_text(String::utf8("メッセージをコピー"));
	copy_button->set_custom_minimum_size(Size2(24, 24) * EDSCALE);
	if (editor_theme.is_valid() && editor_theme->has_icon("ActionCopy", "EditorIcons")) {
		copy_button->set_button_icon(editor_theme->get_icon("ActionCopy", "EditorIcons"));
	} else {
		copy_button->set_text(String::utf8("コピー"));
	}
	copy_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_copy_message).bind(copy_text));
	actions->add_child(copy_button);
	if (is_user) {
		Button *edit_button = memnew(Button);
		edit_button->set_flat(true);
		edit_button->set_tooltip_text(String::utf8("入力欄に戻す"));
		edit_button->set_custom_minimum_size(Size2(24, 24) * EDSCALE);
		if (editor_theme.is_valid() && editor_theme->has_icon("Edit", "EditorIcons")) {
			edit_button->set_button_icon(editor_theme->get_icon("Edit", "EditorIcons"));
		} else {
			edit_button->set_text(String::utf8("編集"));
		}
		edit_button->connect(SceneStringName(pressed), callable_mp(this, &HorrorAIAssistantEditorPlugin::_edit_message).bind(p_text, p_image_paths));
		actions->add_child(edit_button);
	}
	if (transcript_scroll) {
		transcript_scroll->call_deferred("ensure_control_visible", message);
	}
	if (!restoring_session && !current_session_id.is_empty()) {
		Dictionary saved_message;
		saved_message["role"] = p_role;
		saved_message["text"] = p_text;
		saved_message["images"] = p_image_paths;
		conversation_messages.push_back(saved_message);
		_save_current_session();
	}
}

void HorrorAIAssistantEditorPlugin::_copy_message(const String &p_text) {
	DisplayServer *display_server = DisplayServer::get_singleton();
	if (display_server) {
		display_server->clipboard_set(p_text);
	}
}

void HorrorAIAssistantEditorPlugin::_edit_message(const String &p_text, const Array &p_image_paths) {
	if (prompt_input) {
		prompt_input->set_text(p_text);
		prompt_input->grab_focus();
	}
	pending_image_paths = p_image_paths;
	_refresh_image_attachments();
}

String HorrorAIAssistantEditorPlugin::_get_setting(const String &p_name, const String &p_default) const {
	if (saved_settings.is_null()) {
		return p_default;
	}
	int separator = p_name.rfind("/");
	String section = separator >= 0 ? p_name.substr(0, separator) : String();
	String key = separator >= 0 ? p_name.substr(separator + 1) : p_name;
	return saved_settings->get_value(section, key, p_default);
}

void HorrorAIAssistantEditorPlugin::_show_settings() {
	if (!settings_window || !endpoint_input || !deployment_input || !api_key_input) {
		return;
	}
	endpoint_input->set_text(_get_setting("mina/ai/azure_endpoint", ""));
	deployment_input->set_text(_get_setting("mina/ai/azure_deployment", ""));
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	api_key_input->set_text(api_key);
	settings_window->popup_centered_clamped(Size2i(580, 420));
}

void HorrorAIAssistantEditorPlugin::_save_settings() {
	String endpoint = endpoint_input->get_text().strip_edges();
	String deployment = deployment_input->get_text().strip_edges();
	if (!endpoint.is_empty()) {
		saved_settings->set_value("mina/ai", "azure_endpoint", endpoint);
	}
	if (!deployment.is_empty()) {
		saved_settings->set_value("mina/ai", "azure_deployment", deployment);
	}
	String new_api_key = api_key_input->get_text().strip_edges();
	if (new_api_key.is_empty()) {
		new_api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	if (!new_api_key.is_empty()) {
		api_key = new_api_key;
		String encrypted_key = _encrypt_api_key(new_api_key);
#ifdef WINDOWS_ENABLED
		if (encrypted_key.is_empty()) {
			status_label->set_text(String::utf8("APIキーをWindowsで暗号化して保存できませんでした。環境変数 AZURE_OPENAI_API_KEY を使ってください。"));
			status_label->set_visible(true);
			return;
		}
		saved_settings->set_value("mina/ai", "azure_api_key_dpapi", encrypted_key);
#endif
	}
	Error save_error = _save_saved_settings(saved_settings);
	if (save_error != OK) {
		status_label->set_text(vformat(String::utf8("設定を保存できませんでした (%d)。"), save_error));
		status_label->set_visible(true);
		return;
	}
	settings_window->hide();
	status_label->set_text(api_key.is_empty() ? String::utf8("APIキーを入力するか AZURE_OPENAI_API_KEY を設定してください。") : String::utf8("Azure OpenAIの設定を保存しました。"));
	status_label->set_visible(true);
}

void HorrorAIAssistantEditorPlugin::_send_prompt() {
	if (busy) {
		return;
	}
	String prompt = prompt_input->get_text().strip_edges();
	if (prompt.is_empty() && pending_image_paths.is_empty()) {
		return;
	}
	if (api_key.is_empty()) {
		api_key = OS::get_singleton()->get_environment("AZURE_OPENAI_API_KEY");
	}
	if (api_key.is_empty() || _get_setting("mina/ai/azure_endpoint", "").is_empty() || _get_setting("mina/ai/azure_deployment", "").is_empty()) {
		status_label->set_text(String::utf8("Azure OpenAIの接続設定が必要です。"));
		status_label->set_visible(true);
		_show_settings();
		return;
	}
	if (responses_input_items.is_empty() && session_title && session_title->get_text() == String::utf8("新しいセッション")) {
		String title = prompt.is_empty() ? String::utf8("画像について") : prompt.replace("\n", " ").replace("\t", " ").strip_edges();
		if (title.length() > 28) {
			title = title.substr(0, 27) + "…";
		}
		session_title->set_text(title);
	}
	Array message_content;
	String text_content = prompt.is_empty() ? String::utf8("添付画像の内容を確認してください。") : prompt;
	Dictionary text_part;
	text_part["type"] = "input_text";
	text_part["text"] = text_content;
	message_content.push_back(text_part);
	int64_t total_image_bytes = 0;
	for (int i = 0; i < pending_image_paths.size(); i++) {
		String path = pending_image_paths[i];
		String extension = path.get_extension().to_lower();
		if (!_is_supported_image_extension(extension)) {
			_set_busy(false, String::utf8("PNG、JPEG、WebP画像を選択してください。"));
			return;
		}
		Error error = OK;
		Ref<FileAccess> image_file = FileAccess::open(path, FileAccess::READ, &error);
		if (image_file.is_null()) {
			_set_busy(false, vformat(String::utf8("画像を読み込めません: %s"), path.get_file()));
			return;
		}
		int64_t image_length = image_file->get_length();
		if (image_length <= 0 || image_length > MAX_IMAGE_BYTES || total_image_bytes + image_length > MAX_TOTAL_IMAGE_BYTES) {
			_set_busy(false, String::utf8("画像は1枚5 MB以下、合計10 MB以下にしてください。"));
			return;
		}
		PackedByteArray image_bytes = image_file->get_buffer(image_length);
		if (image_bytes.size() != image_length || !CoreBind::Marshalls::get_singleton()) {
			_set_busy(false, vformat(String::utf8("画像を送信データに変換できませんでした: %s"), path.get_file()));
			return;
		}
		Dictionary image_part;
		image_part["type"] = "input_image";
		image_part["image_url"] = "data:" + _get_image_mime_type(extension) + ";base64," + CoreBind::Marshalls::get_singleton()->raw_to_base64(image_bytes);
		message_content.push_back(image_part);
		total_image_bytes += image_length;
	}
	Dictionary response_input_message;
	response_input_message["type"] = "message";
	response_input_message["role"] = "user";
	response_input_message["content"] = message_content;
	if (current_session_id.is_empty()) {
		current_session_id = String::num_uint64(OS::get_singleton()->get_ticks_usec());
		while (session_ids.has(current_session_id)) {
			current_session_id += "x";
		}
	}
	responses_input_items.push_back(response_input_message);
	_append_message("user", prompt, pending_image_paths);
	_clear_image_attachments();
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
		_discard_staged_changes();
		_set_busy(false, String::utf8("Endpointには https:// から始まるAzure OpenAI URLを入力してください。"));
		return;
	}
	String deployment = _get_setting("mina/ai/azure_deployment", "").strip_edges();
	if (deployment.is_empty()) {
		_discard_staged_changes();
		_set_busy(false, String::utf8("Deploymentを設定してください。"));
		return;
	}
	String endpoint_lower = endpoint.to_lower();
	String url = endpoint_lower.ends_with("/openai/v1") ? endpoint + "/responses" : endpoint + "/openai/v1/responses";
	last_request_url = url;
	Dictionary request_body;
	request_body["model"] = deployment;
	request_body["instructions"] = system_instructions;
	request_body["input"] = responses_input_items;
	request_body["tools"] = _get_tool_schemas();
	request_body["tool_choice"] = "auto";
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("api-key: " + api_key);
	Error error = http_request->request(url, headers, HTTPClient::METHOD_POST, JSON::stringify(request_body));
	if (error != OK) {
		responses_input_items.clear();
		_discard_staged_changes();
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
		_discard_staged_changes();
		String error_text;
		String error_code;
		if (parse_error == OK && parser.get_data().get_type() == Variant::DICTIONARY) {
			Dictionary response = parser.get_data();
			Dictionary error = response.get("error", Dictionary());
			error_text = error.get("message", "");
			error_code = error.get("code", "");
		}
		if (error_text.is_empty()) {
			error_text = vformat("HTTP %d (result %d)", p_response_code, p_result);
		}
		String status = String::utf8("Azure OpenAIエラー");
		if (p_response_code > 0) {
			status += vformat(" (HTTP %d", p_response_code);
			if (!error_code.is_empty()) {
				status += ", " + error_code;
			}
			status += ")";
		}
		status += ": " + error_text;
		if (p_response_code == 404) {
			status += String::utf8("\n要求URL: ") + last_request_url;
			status += String::utf8("\nEndpointとDeploymentがAzure上の設定と一致するか確認してください。API versionはResponses APIが自動選択します。");
		}
		responses_input_items.clear();
		_set_busy(false, status);
		return;
	}
	if (parse_error != OK || parser.get_data().get_type() != Variant::DICTIONARY) {
		_discard_staged_changes();
		_set_busy(false, String::utf8("Azure OpenAIの応答をJSONとして読み取れませんでした。"));
		return;
	}
	Dictionary response = parser.get_data();
	_handle_responses_response(response);
}

void HorrorAIAssistantEditorPlugin::_handle_responses_response(const Dictionary &p_response) {
	Array output = p_response.get("output", Array());
	Array function_calls;
	for (int i = 0; i < output.size(); i++) {
		Dictionary item = output[i];
		responses_input_items.push_back(item);
		if (String(item.get("type", "")) == "function_call") {
			function_calls.push_back(item);
		}
	}
	if (!function_calls.is_empty()) {
		if (tool_round >= 6) {
			_discard_staged_changes();
			_set_busy(false, String::utf8("ツール実行回数の上限に達しました。指示を分けて続けてください。"));
			return;
		}
		for (int i = 0; i < function_calls.size(); i++) {
			Dictionary call = function_calls[i];
			String arguments_text = call.get("arguments", "{}");
			JSON arguments_parser;
			Dictionary arguments;
			Dictionary result;
			if (arguments_parser.parse(arguments_text) == OK && arguments_parser.get_data().get_type() == Variant::DICTIONARY) {
				arguments = arguments_parser.get_data();
				String tool_name = call.get("name", "");
				result = _execute_tool(tool_name, arguments);
				_record_tool_activity(tool_name, arguments, result);
			} else {
				result["ok"] = false;
				result["error"] = "Function arguments were not a JSON object.";
			}
			Dictionary tool_output;
			tool_output["type"] = "function_call_output";
			tool_output["call_id"] = call.get("call_id", "");
			tool_output["output"] = JSON::stringify(result);
			responses_input_items.push_back(tool_output);
		}
		tool_round++;
		_set_busy(true, String::utf8("プロジェクトを調査・編集しています…"));
		_request_next_turn();
		return;
	}
	String answer = p_response.get("output_text", "");
	if (answer.is_empty()) {
		for (int i = 0; i < output.size(); i++) {
			Dictionary item = output[i];
			if (String(item.get("type", "")) != "message") {
				continue;
			}
			Array content = item.get("content", Array());
			for (int j = 0; j < content.size(); j++) {
				Dictionary part = content[j];
				String part_type = part.get("type", "");
				if (part_type == "output_text" || part_type == "refusal") {
					answer += String(part.get("text", part.get("refusal", "")));
				}
			}
		}
	}
	if (answer.is_empty()) {
		answer = String::utf8("応答は返りましたが、表示できる本文がありませんでした。");
	}
	_append_message("assistant", answer);
	_set_busy(false);
	if (!staged_files.is_empty()) {
		_apply_changes();
	} else if (status_label) {
		status_label->set_visible(false);
	}
}

void HorrorAIAssistantEditorPlugin::_set_busy(bool p_busy, const String &p_status) {
	busy = p_busy;
	if (send_button) {
		send_button->set_disabled(p_busy);
	}
	if (status_label && !p_status.is_empty()) {
		status_label->set_text(p_status);
		status_label->set_visible(true);
	}
}

void HorrorAIAssistantEditorPlugin::_record_tool_activity(const String &p_name, const Dictionary &p_arguments, const Dictionary &p_result) {
	bool ok = p_result.get("ok", false);
	String path = p_result.get("path", p_arguments.get("path", ""));
	String summary;
	if (!ok) {
		summary = String::utf8("失敗: ") + String(p_result.get("error", "不明なエラー"));
	} else if (p_name == "list_project_files" || p_name == "list_audio_files") {
		Array files = p_result.get("files", Array());
		summary = vformat(String::utf8("%sを一覧しました（%d件）: %s"), p_name == "list_audio_files" ? String::utf8("音声ファイル") : String::utf8("プロジェクトファイル"), files.size(), p_result.get("root", "res://"));
	} else if (p_name == "read_project_file") {
		summary = vformat(String::utf8("ファイルを読み込みました: %s"), path);
	} else if (p_name == "inspect_current_scene") {
		Array nodes = p_result.get("nodes", Array());
		summary = vformat(String::utf8("現在のシーンを確認しました（ノード %d個）: %s"), nodes.size(), p_result.get("scene_path", ""));
	} else if (p_name == "stage_project_file") {
		summary = vformat(String::utf8("ファイルの変更案を作成しました: %s"), path);
	} else {
		summary = vformat(String::utf8("ツールを実行しました: %s"), p_name);
	}

	Dictionary entry;
	entry["kind"] = p_name == "stage_project_file" && ok ? "edit" : "operation";
	entry["summary"] = summary;
	entry["path"] = path;
	entry["ok"] = ok;
	if (p_name == "stage_project_file" && ok && staged_files.has(path)) {
		Dictionary staged = staged_files[path];
		entry["exists"] = staged.get("exists", false);
		entry["original"] = staged.get("original", "");
		entry["content"] = staged.get("content", "");
		entry["reason"] = staged.get("reason", "");
		entry["applied"] = false;
		for (int i = 0; i < activity_entries.size(); i++) {
			Dictionary previous = activity_entries[i];
			if (String(previous.get("kind", "")) == "edit" && String(previous.get("path", "")) == path) {
				activity_entries[i] = entry;
				_refresh_activity_view();
				return;
			}
		}
	}
	activity_entries.push_back(entry);
	_refresh_activity_view();
}

void HorrorAIAssistantEditorPlugin::_refresh_activity_view() {
	if (!activity_button || !activity_details) {
		return;
	}
	int edit_count = 0;
	int operation_count = 0;
	for (int i = 0; i < activity_entries.size(); i++) {
		Dictionary entry = activity_entries[i];
		if (String(entry.get("kind", "")) == "edit") {
			edit_count++;
		} else {
			operation_count++;
		}
	}
	activity_button->set_visible(!activity_entries.is_empty());
	activity_button->set_text(vformat(String::utf8("作業ログを%s（編集 %d・調査 %d）"), activity_details->is_visible() ? String::utf8("隠す") : String::utf8("表示"), edit_count, operation_count));

	String details = String::utf8("Mina AI の作業履歴\n\n調査・読み込み\n");
	for (int i = 0; i < activity_entries.size(); i++) {
		Dictionary entry = activity_entries[i];
		if (String(entry.get("kind", "")) != "edit") {
			details += String::utf8("  • ") + String(entry.get("summary", "")) + "\n";
		}
	}
	if (operation_count == 0) {
		details += String::utf8("  なし\n");
	}
	if (edit_count > 0) {
		details += String::utf8("\nファイル変更履歴\n");
		for (int i = 0; i < activity_entries.size(); i++) {
			Dictionary entry = activity_entries[i];
			if (String(entry.get("kind", "")) != "edit") {
				continue;
			}
			String action = entry.get("discarded", false) ? String::utf8("変更案を破棄しました") : (entry.get("applied", false) ? String::utf8("ファイルを編集しました") : String::utf8("ファイルの変更案を作成しました"));
			details += String(entry.get("exists", false) ? "\n  ~ " : "\n  + ") + action + ": " + String(entry.get("path", "")) + "\n";
			String reason = entry.get("reason", "");
			if (!reason.is_empty()) {
				details += String::utf8("    ") + reason + "\n";
			}
			if (bool(entry.get("exists", false))) {
				details += String::utf8("\n    --- 変更前 ---\n") + String(entry.get("original", "")) + "\n";
			}
			details += String::utf8("\n    +++ 変更後 ---\n") + String(entry.get("content", "")) + "\n";
		}
	}
	activity_details->set_text(details);
}

void HorrorAIAssistantEditorPlugin::_toggle_activity_details() {
	if (!activity_details) {
		return;
	}
	activity_details->set_visible(!activity_details->is_visible());
	_refresh_activity_view();
}

void HorrorAIAssistantEditorPlugin::_discard_staged_changes() {
	if (staged_files.is_empty()) {
		return;
	}
	Array paths = staged_files.keys();
	for (int i = 0; i < activity_entries.size(); i++) {
		Dictionary entry = activity_entries[i];
		if (String(entry.get("kind", "")) == "edit" && paths.has(entry.get("path", "")) && !bool(entry.get("applied", false))) {
			entry["discarded"] = true;
			activity_entries[i] = entry;
		}
	}
	staged_files.clear();
	_refresh_activity_view();
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
	tools.push_back(_function_tool("stage_project_file", "ファイル全体の変更案を作成します。AIの回答完了後に自動適用されます。既存ファイルは先にread_project_fileで読んでください。", properties, required));
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
	result["ok"] = true;
	result["path"] = p_path;
	result["reason"] = p_reason;
	result["characters"] = p_content.length();
	result["message"] = "Change staged; it will be applied after the complete response.";
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
	if (!p_path.begins_with("res://") || p_path.contains("\\")) {
		return false;
	}
	String relative = p_path.trim_prefix("res://");
	if (relative.contains(":")) {
		return false;
	}
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

void HorrorAIAssistantEditorPlugin::_apply_changes() {
	if (staged_files.is_empty()) {
		return;
	}
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	PackedStringArray unsaved_scenes = editor_interface->get_unsaved_scenes();
	ScriptEditor *script_editor = editor_interface->get_script_editor();
	PackedStringArray unsaved_scripts = script_editor ? script_editor->get_unsaved_files() : PackedStringArray();
	Array paths = staged_files.keys();
	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary entry = staged_files[path];
		bool file_exists = FileAccess::exists(path);
		if (file_exists != bool(entry.get("exists", false)) || (!file_exists && DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(path)))) {
			_set_busy(false, vformat(String::utf8("変更前にファイル状態が変わりました。安全のため未適用の案を破棄しました: %s"), path));
			_discard_staged_changes();
			return;
		}
		if (file_exists) {
			Error read_error = OK;
			Ref<FileAccess> current_file = FileAccess::open(path, FileAccess::READ, &read_error);
			if (current_file.is_null() || current_file->get_length() > 524288 || current_file->get_as_text() != String(entry.get("original", ""))) {
				_set_busy(false, vformat(String::utf8("変更前にファイル内容が変わりました。安全のため未適用の案を破棄しました: %s"), path));
				_discard_staged_changes();
				return;
			}
		}
		if (!path.ends_with(".tscn")) {
			continue;
		}
		if (unsaved_scenes.has(path) || unsaved_scenes.has(ProjectSettings::get_singleton()->globalize_path(path))) {
			_set_busy(false, String::utf8("未保存のシーンがあるため適用できません。シーンを保存してから再度依頼してください。"));
			_discard_staged_changes();
			return;
		}
		if (unsaved_scripts.has(path) || unsaved_scripts.has(ProjectSettings::get_singleton()->globalize_path(path))) {
			_set_busy(false, String::utf8("未保存のスクリプトがあるため適用できません。スクリプトを保存してから再度依頼してください。"));
			_discard_staged_changes();
			return;
		}
	}

	String backup_root = OS::get_singleton()->get_user_data_dir().path_join("mina_ai_backups").path_join(String::num_uint64(OS::get_singleton()->get_ticks_msec()));
	Error error = DirAccess::make_dir_recursive_absolute(backup_root);
	if (error != OK) {
		_set_busy(false, vformat(String::utf8("バックアップ用フォルダーを作れませんでした (%d)。未適用の案を破棄しました。"), error));
		_discard_staged_changes();
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
			_set_busy(false, vformat(String::utf8("バックアップ先を作れませんでした。未適用の案を破棄しました: %s"), backup_path));
			_discard_staged_changes();
			return;
		}
		Ref<FileAccess> backup = FileAccess::open(backup_path, FileAccess::WRITE, &error);
		if (backup.is_null() || !backup->store_string(entry.get("original", ""))) {
			_set_busy(false, vformat(String::utf8("既存ファイルのバックアップに失敗しました。未適用の案を破棄しました: %s"), path));
			_discard_staged_changes();
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
		_set_busy(false, vformat(String::utf8("適用に失敗したため元の内容へ戻しました。未適用の案を破棄しました。エラー: %d"), error));
		_discard_staged_changes();
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
	for (int i = 0; i < activity_entries.size(); i++) {
		Dictionary entry = activity_entries[i];
		if (String(entry.get("kind", "")) == "edit" && paths.has(entry.get("path", ""))) {
			entry["applied"] = true;
			entry["summary"] = vformat(String::utf8("ファイルを編集しました: %s"), entry.get("path", ""));
			activity_entries[i] = entry;
		}
	}
	staged_files.clear();
	_refresh_activity_view();
	status_label->set_visible(false);
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

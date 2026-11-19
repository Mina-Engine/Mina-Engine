/**************************************************************************/
/*  horror_director_editor_plugin.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            MINA ENGINE                                  */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#include "horror_director_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/slider.h"

void HorrorDirectorEditorPlugin::_build_panel() {
	dock = memnew(EditorDock);
	dock->set_name("HorrorDirectorDock");
	dock->set_title("Horror Director");
	dock->set_layout_key("horror_director");
	dock->set_icon_name("MinaEngine");
	dock->set_default_slot(EditorDock::DOCK_SLOT_RIGHT_BR);
	dock->set_custom_minimum_size(Size2(260, 0) * EDSCALE);

	VBoxContainer *layout = memnew(VBoxContainer);
	dock->add_child(layout);

	Label *intro = memnew(Label);
	intro->set_text(String::utf8("ゲーム全体の緊張度と、使える演出候補を調整します。"));
	intro->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(intro);
	layout->add_child(memnew(HSeparator));

	Label *profile_title = memnew(Label);
	profile_title->set_text(String::utf8("ホラーの方向性"));
	layout->add_child(profile_title);

	profile_selector = memnew(OptionButton);
	profile_selector->set_accessibility_name(String::utf8("ホラーの方向性"));
	profile_selector->add_item(String::utf8("心理ホラー"));
	profile_selector->add_item(String::utf8("サバイバルホラー"));
	profile_selector->add_item(String::utf8("監視・異変発見ホラー"));
	profile_selector->add_item(String::utf8("探索・追跡ホラー"));
	profile_selector->select(0);
	profile_selector->connect(SNAME("item_selected"), callable_mp(this, &HorrorDirectorEditorPlugin::_on_profile_selected));
	layout->add_child(profile_selector);

	layout->add_child(memnew(HSeparator));
	HBoxContainer *tension_row = memnew(HBoxContainer);
	Label *tension_title = memnew(Label);
	tension_title->set_text(String::utf8("緊張度"));
	tension_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tension_row->add_child(tension_title);
	tension_summary = memnew(Label);
	tension_summary->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	tension_summary->set_accessibility_name(String::utf8("現在の緊張度"));
	tension_row->add_child(tension_summary);
	layout->add_child(tension_row);

	tension_slider = memnew(HSlider);
	tension_slider->set_accessibility_name(String::utf8("緊張度"));
	tension_slider->set_min(0.0);
	tension_slider->set_max(100.0);
	tension_slider->set_step(1.0);
	tension_slider->set_value(70.0);
	tension_slider->set_tooltip_text(String::utf8("演出候補のプレビューに使う緊張度です。"));
	tension_slider->connect(SNAME("value_changed"), callable_mp(this, &HorrorDirectorEditorPlugin::_on_tension_changed));
	layout->add_child(tension_slider);

	Label *candidate_title = memnew(Label);
	candidate_title->set_text(String::utf8("この緊張度で使える演出候補"));
	layout->add_child(candidate_title);

	for (int i = 0; i < 3; i++) {
		candidate_labels[i] = memnew(Label);
		candidate_labels[i]->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		candidate_labels[i]->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
		layout->add_child(candidate_labels[i]);
	}

	layout->add_child(memnew(HSeparator));
	Label *preview_note = memnew(Label);
	preview_note->set_text(String::utf8("演出候補の設計プレビューです。時間軸での自動実行は次の実装項目です。"));
	preview_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	layout->add_child(preview_note);

	EditorDockManager::get_singleton()->add_dock(dock);
	dock->make_visible();
	_refresh_preview();
}

void HorrorDirectorEditorPlugin::_refresh_preview() {
	static const char *candidate_text[4][3][3] = {
		{
			{ "環境音を少しずつ弱める", "遠くで一度だけ物音を鳴らす", "視界の端に一瞬だけ影を見せる" },
			{ "離れた場所で足音を近づける", "照明を一度だけ点滅させる", "通った後の扉を閉める" },
			{ "逃げ道の先へ音で誘導する", "背後に影を出して姿を隠す", "照明が落ちた直後に無音にする" },
		},
		{
			{ "敵が足音を聞いて巡回する", "懐中電灯の光に反応する", "見失うと探索を諦める" },
			{ "プレイヤーの痕跡を調べる", "遮蔽物を選んで接近する", "短時間の追跡を始める" },
			{ "隠れ場所を調べて追い詰める", "追跡を強めて退路をふさぐ", "物音で別の経路へ誘導する" },
		},
		{
			{ "映像の端に小さな異変を出す", "別のカメラに一瞬だけ姿を映す", "短いノイズを挟む" },
			{ "廊下の奥に異常を出す", "切替時にだけ音を鳴らす", "映像の端に影を映す" },
			{ "複数のカメラに異常を連続させる", "監視画面に敵を映す", "映像を一度途切れさせる" },
		},
		{
			{ "調べられる手掛かりを音で示す", "遠くの扉を一度だけ開閉する", "懐中電灯の光を誘導する" },
			{ "戻り道に足音を残す", "目的地の先に痕跡を置く", "行き止まりの奥で音を鳴らす" },
			{ "退路の先へ物音で誘導する", "背後から足音を近づける", "進行先の照明を消す" },
		},
	};

	const double tension = tension_slider->get_value();
	String stage;
	if (tension < 20.0) {
		stage = String::utf8("静穏");
	} else if (tension < 40.0) {
		stage = String::utf8("違和感");
	} else if (tension < 60.0) {
		stage = String::utf8("不安");
	} else if (tension < 85.0) {
		stage = String::utf8("強い緊張");
	} else {
		stage = String::utf8("危機");
	}
	tension_summary->set_text(vformat("%d%% - %s", int(tension), stage));

	const int profile = CLAMP(profile_selector->get_selected(), 0, 3);
	const int band = tension < 30.0 ? 0 : tension < 65.0 ? 1 : 2;
	for (int i = 0; i < 3; i++) {
		candidate_labels[i]->set_text(String::utf8("• ") + String::utf8(candidate_text[profile][band][i]));
	}
}

void HorrorDirectorEditorPlugin::_on_profile_selected(int p_index) {
	_refresh_preview();
}

void HorrorDirectorEditorPlugin::_on_tension_changed(double p_value) {
	_refresh_preview();
}

void HorrorDirectorEditorPlugin::_notification(int p_what) {
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

/**************************************************************************/
/*  horror_director_editor_plugin.h                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            MINA ENGINE                                  */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class EditorDock;
class HSlider;
class Label;
class OptionButton;

class HorrorDirectorEditorPlugin : public EditorPlugin {
	GDCLASS(HorrorDirectorEditorPlugin, EditorPlugin);

	EditorDock *dock = nullptr;
	HSlider *tension_slider = nullptr;
	Label *tension_summary = nullptr;
	OptionButton *profile_selector = nullptr;
	Label *candidate_labels[3] = {};

	void _build_panel();
	void _refresh_preview();
	void _on_profile_selected(int p_index);
	void _on_tension_changed(double p_value);

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	HorrorDirectorEditorPlugin() = default;
};

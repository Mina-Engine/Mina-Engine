/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           MINA HORROR ENGINE                            */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#include "register_types.h"

#ifdef TOOLS_ENABLED
#include "editor/horror_director_editor_plugin.h"
#include "editor/horror_ai_assistant_editor_plugin.h"
#endif
#include "core/object/class_db.h"
#include "horror_interactable_3d.h"
#include "horror_interactor_3d.h"

void initialize_horror_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(HorrorInteractable3D);
		GDREGISTER_CLASS(HorrorInteractor3D);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<HorrorDirectorEditorPlugin>();
		EditorPlugins::add_by_type<HorrorAIAssistantEditorPlugin>();
	}
#endif
}

void uninitialize_horror_module(ModuleInitializationLevel p_level) {
	// Nothing to do here.
}

/**************************************************************************/
/*  horror_interactable_3d.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           MINA HORROR ENGINE                            */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#include "horror_interactable_3d.h"

#include "core/object/class_db.h"

void HorrorInteractable3D::set_interaction_prompt(const String &p_prompt) {
	interaction_prompt = p_prompt;
}

String HorrorInteractable3D::get_interaction_prompt() const {
	return interaction_prompt;
}

void HorrorInteractable3D::set_interaction_enabled(bool p_enabled) {
	interaction_enabled = p_enabled;
}

bool HorrorInteractable3D::is_interaction_enabled() const {
	return interaction_enabled;
}

void HorrorInteractable3D::interact(Node3D *p_interactor) {
	if (interaction_enabled) {
		emit_signal("interacted", p_interactor);
	}
}

void HorrorInteractable3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_interaction_prompt", "prompt"), &HorrorInteractable3D::set_interaction_prompt);
	ClassDB::bind_method(D_METHOD("get_interaction_prompt"), &HorrorInteractable3D::get_interaction_prompt);
	ClassDB::bind_method(D_METHOD("set_interaction_enabled", "enabled"), &HorrorInteractable3D::set_interaction_enabled);
	ClassDB::bind_method(D_METHOD("is_interaction_enabled"), &HorrorInteractable3D::is_interaction_enabled);
	ClassDB::bind_method(D_METHOD("interact", "interactor"), &HorrorInteractable3D::interact);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "interaction_prompt"), "set_interaction_prompt", "get_interaction_prompt");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "interaction_enabled"), "set_interaction_enabled", "is_interaction_enabled");

	ADD_SIGNAL(MethodInfo("interacted", PropertyInfo(Variant::OBJECT, "interactor")));
}

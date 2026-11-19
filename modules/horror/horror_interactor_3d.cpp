/**************************************************************************/
/*  horror_interactor_3d.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           MINA HORROR ENGINE                            */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#include "horror_interactor_3d.h"

#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "scene/main/node.h"
#include "horror_interactable_3d.h"

void HorrorInteractor3D::set_interaction_action(const StringName &p_action) {
	interaction_action = p_action;
	update_configuration_warnings();
}

StringName HorrorInteractor3D::get_interaction_action() const {
	return interaction_action;
}

void HorrorInteractor3D::set_interaction_distance(real_t p_distance) {
	ERR_FAIL_COND_MSG(!Math::is_finite(p_distance), "Interaction distance must be finite.");
	interaction_distance = MAX((real_t)0.05, p_distance);
	set_target_position(Vector3(0, 0, -interaction_distance));
}

real_t HorrorInteractor3D::get_interaction_distance() const {
	return interaction_distance;
}

Node *HorrorInteractor3D::get_current_target() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(current_target_id));
}

String HorrorInteractor3D::get_current_prompt() const {
	return current_prompt;
}

HorrorInteractable3D *HorrorInteractor3D::find_interactable(Object *p_collider) const {
	Node *node = Object::cast_to<Node>(p_collider);
	while (node) {
		HorrorInteractable3D *interactable = Object::cast_to<HorrorInteractable3D>(node);
		if (interactable && interactable->is_interaction_enabled()) {
			return interactable;
		}
		node = node->get_parent();
	}
	return nullptr;
}

void HorrorInteractor3D::update_target(HorrorInteractable3D *p_target) {
	const ObjectID next_target_id = p_target ? p_target->get_instance_id() : ObjectID();
	const String next_prompt = p_target ? p_target->get_interaction_prompt() : String();
	if (next_target_id == current_target_id && next_prompt == current_prompt) {
		return;
	}

	current_target_id = next_target_id;
	current_prompt = next_prompt;
	emit_signal("target_changed", p_target, current_prompt);
}

bool HorrorInteractor3D::interact() {
	HorrorInteractable3D *target = Object::cast_to<HorrorInteractable3D>(get_current_target());
	if (!target || !target->is_interaction_enabled()) {
		return false;
	}

	target->interact(this);
	return true;
}

PackedStringArray HorrorInteractor3D::get_configuration_warnings() const {
	PackedStringArray warnings = RayCast3D::get_configuration_warnings();
	InputMap *input_map = InputMap::get_singleton();
	if (!interaction_action.is_empty() && input_map && !input_map->has_action(interaction_action)) {
		warnings.push_back(vformat("Interaction action '%s' is not defined in the Input Map.", interaction_action));
	}
	return warnings;
}

void HorrorInteractor3D::_notification(int p_what) {
	RayCast3D::_notification(p_what);
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_physics_process(true);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			update_target(nullptr);
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			HorrorInteractable3D *target = is_colliding() ? find_interactable(get_collider()) : nullptr;
			update_target(target);

			InputMap *input_map = InputMap::get_singleton();
			Input *input = Input::get_singleton();
			if (target && !interaction_action.is_empty() && input_map && input && input_map->has_action(interaction_action) &&
					input->is_action_just_pressed(interaction_action)) {
				interact();
			}
		} break;
	}
}

void HorrorInteractor3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_interaction_action", "action"), &HorrorInteractor3D::set_interaction_action);
	ClassDB::bind_method(D_METHOD("get_interaction_action"), &HorrorInteractor3D::get_interaction_action);
	ClassDB::bind_method(D_METHOD("set_interaction_distance", "distance"), &HorrorInteractor3D::set_interaction_distance);
	ClassDB::bind_method(D_METHOD("get_interaction_distance"), &HorrorInteractor3D::get_interaction_distance);
	ClassDB::bind_method(D_METHOD("get_current_target"), &HorrorInteractor3D::get_current_target);
	ClassDB::bind_method(D_METHOD("get_current_prompt"), &HorrorInteractor3D::get_current_prompt);
	ClassDB::bind_method(D_METHOD("interact"), &HorrorInteractor3D::interact);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "interaction_action"), "set_interaction_action", "get_interaction_action");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interaction_distance", PROPERTY_HINT_RANGE, "0.05,10,0.05,or_greater,suffix:m"), "set_interaction_distance", "get_interaction_distance");

	ADD_SIGNAL(MethodInfo("target_changed", PropertyInfo(Variant::OBJECT, "target"), PropertyInfo(Variant::STRING, "prompt")));
}

HorrorInteractor3D::HorrorInteractor3D() {
	set_collide_with_areas(true);
	set_target_position(Vector3(0, 0, -interaction_distance));
}

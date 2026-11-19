/**************************************************************************/
/*  horror_interactor_3d.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           MINA HORROR ENGINE                            */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#pragma once

#include "scene/3d/physics/ray_cast_3d.h"

class HorrorInteractable3D;

class HorrorInteractor3D : public RayCast3D {
	GDCLASS(HorrorInteractor3D, RayCast3D);

	StringName interaction_action = "interact";
	real_t interaction_distance = 2.0;
	ObjectID current_target_id;
	String current_prompt;

	HorrorInteractable3D *find_interactable(Object *p_collider) const;
	void update_target(HorrorInteractable3D *p_target);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_interaction_action(const StringName &p_action);
	StringName get_interaction_action() const;

	void set_interaction_distance(real_t p_distance);
	real_t get_interaction_distance() const;

	Node *get_current_target() const;
	String get_current_prompt() const;
	bool interact();

	PackedStringArray get_configuration_warnings() const override;

	HorrorInteractor3D();
};

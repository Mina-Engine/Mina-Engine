/**************************************************************************/
/*  horror_interactable_3d.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           MINA HORROR ENGINE                            */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mina Engine contributors.                   */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"

class HorrorInteractable3D : public Node3D {
	GDCLASS(HorrorInteractable3D, Node3D);

	String interaction_prompt = "Interact";
	bool interaction_enabled = true;

protected:
	static void _bind_methods();

public:
	void set_interaction_prompt(const String &p_prompt);
	String get_interaction_prompt() const;

	void set_interaction_enabled(bool p_enabled);
	bool is_interaction_enabled() const;

	void interact(Node3D *p_interactor);

	HorrorInteractable3D() = default;
};

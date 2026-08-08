/*
 * Boots graphical environment lifecycle and hardware abstraction
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-beui.h"

#include <string.h>

struct boot98_beui_state {
	const struct boot98_beui_hal *hal;
	struct boot98_beui_display_info display;
	int display_open;
	int pointer_open;
	int audio_open;
};

static struct boot98_beui_state state;

int
boot98_beui_bind(const struct boot98_beui_hal *hal)
{
	if (state.display_open || state.pointer_open || state.audio_open)
		return 0;
	state.hal = hal;
	return 1;
}

int
boot98_beui_init(void)
{
	if (state.display_open)
		return 1;
	if (state.hal == NULL || state.hal->display.enter == NULL ||
	    state.hal->display.leave == NULL)
		return 0;
	memset(&state.display, 0, sizeof(state.display));
	if (!state.hal->display.enter(state.hal->display.context,
				      &state.display))
		return 0;
	state.display_open = 1;
	if (state.display.width == 0 || state.display.height == 0)
		goto fail;
	if (state.hal->pointer.start != NULL) {
		if (!state.hal->pointer.start(state.hal->pointer.context,
					      &state.display))
			goto fail;
		state.pointer_open = 1;
	}
	return 1;

fail:
	boot98_beui_close();
	return 0;
}

void
boot98_beui_close(void)
{
	if (state.hal == NULL)
		return;
	if (state.audio_open && state.hal->audio.stop != NULL)
		state.hal->audio.stop(state.hal->audio.context);
	state.audio_open = 0;
	if (state.pointer_open && state.hal->pointer.stop != NULL)
		state.hal->pointer.stop(state.hal->pointer.context);
	state.pointer_open = 0;
	if (state.display_open && state.hal->display.leave != NULL)
		state.hal->display.leave(state.hal->display.context);
	state.display_open = 0;
	memset(&state.display, 0, sizeof(state.display));
}

void
boot98_beui_cleanup(void)
{
	boot98_beui_close();
	memset(&state, 0, sizeof(state));
}

int
boot98_beui_is_open(void)
{
	return state.display_open;
}

int
boot98_beui_get_display_info(struct boot98_beui_display_info *info)
{
	if (!state.display_open || info == NULL)
		return 0;
	*info = state.display;
	return 1;
}

int
boot98_beui_poll(void)
{
	struct boot98_beui_pointer_event event;

	if (!state.display_open)
		return 0;
	if (state.pointer_open && state.hal->pointer.poll != NULL) {
		memset(&event, 0, sizeof(event));
		if (state.hal->pointer.poll(state.hal->pointer.context, &event) < 0)
			return 0;
	}
	if (state.audio_open && state.hal->audio.poll != NULL &&
	    !state.hal->audio.poll(state.hal->audio.context))
		return 0;
	return 1;
}

int
boot98_beui_flush(void)
{
	if (!state.display_open)
		return 0;
	if (state.hal->display.flush == NULL)
		return 1;
	return state.hal->display.flush(state.hal->display.context, NULL, 0);
}

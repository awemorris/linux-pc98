/*
 * Boots BeUI G1 lifecycle host test
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#include "boot98-beui.h"

#include <string.h>

struct mock_hal {
	int enter_result;
	int pointer_result;
	unsigned enter_count;
	unsigned leave_count;
	unsigned pointer_start_count;
	unsigned pointer_stop_count;
	unsigned pointer_poll_count;
	unsigned flush_count;
};

static int display_enter(void *context, struct boot98_beui_display_info *info)
{
	struct mock_hal *mock = context;

	mock->enter_count++;
	if (!mock->enter_result)
		return 0;
	info->width = 640;
	info->height = 400;
	info->bits_per_pixel = 4;
	info->stride = 80;
	return 1;
}

static void display_leave(void *context)
{
	((struct mock_hal *)context)->leave_count++;
}

static int display_flush(void *context,
			 const struct boot98_beui_rect *rectangles,
			 size_t rectangle_count)
{
	struct mock_hal *mock = context;

	if (rectangles != NULL || rectangle_count != 0)
		return 0;
	mock->flush_count++;
	return 1;
}

static int pointer_start(void *context,
			 const struct boot98_beui_display_info *display)
{
	struct mock_hal *mock = context;

	mock->pointer_start_count++;
	return mock->pointer_result && display->width == 640 &&
		display->height == 400;
}

static void pointer_stop(void *context)
{
	((struct mock_hal *)context)->pointer_stop_count++;
}

static int pointer_poll(void *context, struct boot98_beui_pointer_event *event)
{
	struct mock_hal *mock = context;

	mock->pointer_poll_count++;
	event->delta_x = 1;
	return 1;
}

static struct boot98_beui_hal make_hal(struct mock_hal *mock)
{
	struct boot98_beui_hal hal;

	memset(&hal, 0, sizeof(hal));
	hal.display.context = mock;
	hal.display.enter = display_enter;
	hal.display.leave = display_leave;
	hal.display.flush = display_flush;
	hal.pointer.context = mock;
	hal.pointer.start = pointer_start;
	hal.pointer.stop = pointer_stop;
	hal.pointer.poll = pointer_poll;
	return hal;
}

int main(void)
{
	struct boot98_beui_display_info info;
	struct mock_hal mock;
	struct boot98_beui_hal hal;

	memset(&mock, 0, sizeof(mock));
	mock.enter_result = 1;
	mock.pointer_result = 1;
	hal = make_hal(&mock);
	if (boot98_beui_init() || !boot98_beui_bind(&hal) ||
	    !boot98_beui_init() || !boot98_beui_init() ||
	    !boot98_beui_is_open() ||
	    !boot98_beui_get_display_info(&info) || info.width != 640 ||
	    info.height != 400 || !boot98_beui_poll() ||
	    !boot98_beui_flush())
		return 1;
	if (mock.enter_count != 1 || mock.pointer_start_count != 1 ||
	    mock.pointer_poll_count != 1 || mock.flush_count != 1)
		return 2;
	boot98_beui_close();
	boot98_beui_close();
	if (boot98_beui_is_open() || mock.pointer_stop_count != 1 ||
	    mock.leave_count != 1)
		return 3;
	boot98_beui_cleanup();

	memset(&mock, 0, sizeof(mock));
	mock.enter_result = 1;
	hal = make_hal(&mock);
	if (!boot98_beui_bind(&hal) || boot98_beui_init() ||
	    mock.enter_count != 1 || mock.pointer_start_count != 1 ||
	    mock.pointer_stop_count != 0 || mock.leave_count != 1)
		return 4;
	boot98_beui_cleanup();
	return 0;
}

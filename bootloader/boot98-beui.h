/*
 * Boots graphical environment lifecycle and hardware abstraction
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_BEUI_H
#define BOOT98_BEUI_H

#include <stddef.h>
#include <stdint.h>

struct boot98_beui_rect {
	unsigned x;
	unsigned y;
	unsigned width;
	unsigned height;
};

struct boot98_beui_display_info {
	unsigned width;
	unsigned height;
	unsigned bits_per_pixel;
	unsigned stride;
};

enum boot98_beui_image_format {
	BOOT98_BEUI_IMAGE_INDEX8 = 1,
	BOOT98_BEUI_IMAGE_RGB24 = 2,
};

/*
 * Images use a target-independent representation.  Indexed images always
 * contain one palette index per pixel, including BMP sources stored at one or
 * four bits per pixel.  RGB24 pixels are tightly packed in R, G, B order.
 * Palette entries and solid colors use 0x00RRGGBB.
 */
struct boot98_beui_image {
	enum boot98_beui_image_format format;
	unsigned width;
	unsigned height;
	size_t stride;
	const uint8_t *pixels;
	uint32_t palette[256];
	unsigned palette_size;
};

struct boot98_beui_pointer_event {
	int delta_x;
	int delta_y;
	unsigned buttons;
};

/*
 * G1 fixes the ABI before the register-level StratoHAL-derived backends are
 * imported.  A backend owns mode save/restore in enter()/leave().
 */
struct boot98_beui_display_hal {
	void *context;
	int (*enter)(void *context, struct boot98_beui_display_info *info);
	void (*leave)(void *context);
	int (*fill)(void *context, const struct boot98_beui_rect *rect,
		    uint32_t color);
	int (*line)(void *context, unsigned x0, unsigned y0, unsigned x1,
		    unsigned y1, uint32_t color);
	int (*pattern_fill)(void *context, const struct boot98_beui_rect *rect,
			    uint32_t color, uint64_t pattern);
	int (*draw_image)(void *context, unsigned x, unsigned y,
			  const struct boot98_beui_image *image);
	int (*draw_image_pattern)(void *context, unsigned x, unsigned y,
				  const struct boot98_beui_image *image,
				  uint64_t pattern);
	int (*flush)(void *context, const struct boot98_beui_rect *rectangles,
		     size_t rectangle_count);
};

struct boot98_beui_glyph_hal {
	void *context;
	int (*measure)(void *context, uint32_t codepoint, unsigned *width,
		       unsigned *height);
	int (*draw)(void *context, unsigned x, unsigned y, uint32_t codepoint,
		    uint32_t foreground, uint32_t background);
};

struct boot98_beui_pointer_hal {
	void *context;
	int (*start)(void *context,
		     const struct boot98_beui_display_info *display);
	void (*stop)(void *context);
	int (*poll)(void *context, struct boot98_beui_pointer_event *event);
};

struct boot98_beui_clock_hal {
	void *context;
	uint64_t (*milliseconds)(void *context);
};

struct boot98_beui_audio_hal {
	void *context;
	int (*start)(void *context, unsigned sample_rate, unsigned channels);
	void (*stop)(void *context);
	int (*poll)(void *context);
	int (*write)(void *context, const int16_t *samples, size_t frame_count);
};

struct boot98_beui_hal {
	struct boot98_beui_display_hal display;
	struct boot98_beui_glyph_hal glyph;
	struct boot98_beui_pointer_hal pointer;
	struct boot98_beui_clock_hal clock;
	struct boot98_beui_audio_hal audio;
};

/* Bind is per-Noct-VM.  It never probes or changes hardware. */
int boot98_beui_bind(const struct boot98_beui_hal *hal);
int boot98_beui_init(void);
void boot98_beui_close(void);
void boot98_beui_cleanup(void);
int boot98_beui_is_open(void);
int boot98_beui_get_display_info(struct boot98_beui_display_info *info);
int boot98_beui_fill(const struct boot98_beui_rect *rect, uint32_t color);
int boot98_beui_line(unsigned x0, unsigned y0, unsigned x1, unsigned y1,
		     uint32_t color);
int boot98_beui_pattern_fill(const struct boot98_beui_rect *rect,
			     uint32_t color, uint64_t pattern);
int boot98_beui_draw_image(unsigned x, unsigned y,
			    const struct boot98_beui_image *image);
int boot98_beui_draw_image_pattern(unsigned x, unsigned y,
				    const struct boot98_beui_image *image,
				    uint64_t pattern);
int boot98_beui_measure_text(const char *text, unsigned *width,
	unsigned *height);
int boot98_beui_draw_text(const char *text, unsigned x, unsigned y,
	uint32_t foreground, uint32_t background);
int boot98_beui_poll(void);
int boot98_beui_flush(void);

#endif

/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#ifndef BFS_COLOR_H
#define BFS_COLOR_H

#include "bftw.h"

/**
 * A lookup table for colors.
 */
struct color_table;

/**
 * Parse a color table.
 *
 * @param ls_color
 *         A color table in the LS_COLORS environment variable format.
 * @return The parsed color table.
 */
struct color_table *parse_colors(char *ls_colors);

/**
 * Pretty-print a file path.
 *
 * @param colors
 *         The color table to use.
 * @param ftwbuf
 *         The bftw() data for the current path.
 */
void pretty_print(const struct color_table *colors, const struct BFTW *ftwbuf);

/**
 * Pretty-print an error.
 *
 * @param colors
 *         The color table to use.
 * @param path
 *         The file path in error.
 * @param error
 *         The error code that occurred.
 */
void print_error(const struct color_table *colors, const char *path, int error);

/**
 * Free a color table.
 *
 * @param colors
 *         The color table to free.
 */
void free_colors(struct color_table *colors);

#endif // BFS_COLOR_H

/***************************************************************************
 *   Copyright (C) 2010 by Mika Haulo                                      *
 *   mika@haulo.fi                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef THINKLIGHTD_H
#define THINKLIGHTD_H

char* find_keyboard();
int daemonize();
void read_params(int argc, char **argv);
void init_resources();
void cleanup_resources();
int semaphore_wait();
int semaphore_release();
int is_keyboard(char* device);
int frequency_to_brightness(float frequency);

void handle_child_signal(int signal);
void handle_parent_signal(int signal);

void count_strokes();
void monitor_mouse();
void monitor_filesys();

void control_brightness();

#endif

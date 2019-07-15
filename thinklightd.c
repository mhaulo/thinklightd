/***************************************************************************
 *   Copyright (C) 2010 by Mika Haulo                                      *
 *   mika@haulo.fi                                                         *
 *                                                                         *
 *   Keyboard detection based on code by                                   *
 *   Copyright (c) 2005 Brad Hards <bradh@frogmouth.net>                   *
 *   Copyright (c) 2005-2007 Andreas Schneider <mail@cynapses.org>         *
 *                                                                         * 
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

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <usb.h>
#include <linux/input.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/inotify.h>

#include "thinklightd.h"
#include "thinklight.h"

#define KEYBOARD_INTERFACE_CLASS 		3
#define KEYBOARD_INTERFACE_PROTOCOL 	1
#define RESOLUTION 						500000
#define DECAY_FACTOR 					2
#define EV_KEY_DOWN 					1
#define UNPRIVILEGED_USER  				65534 /* nobody */
#define UNPRIVILEGED_GROUP 				65534 /* nobody */
#define DEFAULT_FILESYS_WATCH			"/"

/* this macro is used to tell if "bit" is set in "array"
* it selects a byte from the array, and does a boolean AND
* operation with a byte that only has the relevant bit set.
* eg. to check for the 12th bit, we do (array[1] & 1<<4)
*/
#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))


static int semaphore_id;
static int shared_mem_id;
int keep_going = 1;
int monitor_keyboard_activity = 0;
int monitor_mouse_activity = 0;
int monitor_filesys_activity = 0;
char* filesys_watch = NULL;

int is_keyboard(char* device)
{
	int fd = -1;
	uint8_t key_bitmask[KEY_MAX/8 + 1];
	uint8_t rel_bitmask[REL_MAX/8 + 1];

	if ((fd = open(device, O_RDONLY)) < 0)
	{
		perror("evdev open");
		return -1;
	}

	memset(key_bitmask, 0, sizeof(key_bitmask));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) < 0)
	{
		perror("evdev ioctl");
		return -1;
	}

	memset(rel_bitmask, 0, sizeof(rel_bitmask));
	if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask) < 0)
	{
		perror("evdev ioctl");
		return -1;
	}

	int has_esc = 0;
	int has_space = 0;
	int has_f1 = 0;
	int has_a = 0;

	if (test_bit(KEY_ESC, key_bitmask))
		has_esc = 1;

	if (test_bit(KEY_A, key_bitmask))
		has_a = 1;

	if (test_bit(KEY_SPACE, key_bitmask))
		has_space = 1;

	if (test_bit(KEY_F1, key_bitmask))
		has_f1 = 1;

	int is_keyboard = 0;

	if (has_esc == 1 && has_space == 1 && has_f1 == 1 && has_a == 1)
		is_keyboard = 1;

	return is_keyboard;
}

char* find_keyboard()
{
	
	int index = 0;
	char* input_dev_path_base = "/dev/input/event";
	char* keyboard_device = (char*)malloc(strlen(input_dev_path_base)+3);
	int keyboard_found = 0;
	
	while (keyboard_found == 0)
	{
		sprintf(keyboard_device, "%s%d", input_dev_path_base, index);
		keyboard_found = is_keyboard(keyboard_device);
		index++;
	}
	
	return keyboard_device;
}



int daemonize()
{
	pid_t pid;
	pid = fork();

	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}
	else if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}

	umask(0);

	pid_t sid;
	sid = setsid();

	if (sid < 0)
	{
		exit(EXIT_FAILURE);
	}

	int chdir_result;
	chdir_result = chdir("/");

	if (chdir_result < 0)
	{
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	return 0;
}


int semaphore_wait()
{
	struct sembuf sops = {0, -1, 0};
	int result = semop(semaphore_id, &sops, 1);
	
	return result;
}


int semaphore_release()
{
	struct sembuf sops = {0, 1, 0};
	int result = semop(semaphore_id, &sops, 1);

	return result;
}


void count_strokes()
{
	char* keyboard = find_keyboard();
	int fd = open(keyboard, O_RDONLY);
	free(keyboard);
	struct input_event event;
	char* shm = shmat(shared_mem_id, 0, 0);
	int hitcount;
	
	while(keep_going && read(fd, &event, sizeof(struct input_event)) > 0)
	{
		if(event.type == EV_KEY && event.value == EV_KEY_DOWN)
		{
			if (semaphore_wait() >= 0)
			{
				memcpy(&hitcount, shm, sizeof(int));
				hitcount++;
				memcpy(shm, &hitcount, sizeof(int));
				semaphore_release();
			}

		}
	}
}


void control_brightness()
{
	int hits;
	int decay;
	char* shm = shmat(shared_mem_id, 0, 0);
	
	while(keep_going)
	{
		if (semaphore_wait() >= 0)
		{
			memcpy(&hits, shm, sizeof(int));
			decay = hits/DECAY_FACTOR;
			memcpy(shm, &decay, sizeof(int));
			semaphore_release();
		}

		float frequency = (float)hits/RESOLUTION * 1000000;
		int brightness = frequency_to_brightness(frequency);
		thinklight_set_brightness(brightness);
		usleep(RESOLUTION);
	}
}



void monitor_mouse()
{
	char* mouse = "/dev/input/mice";
	int fd = open(mouse, O_RDONLY);
	struct input_event event;
	char* shm = shmat(shared_mem_id, 0, 0);
	int action;
	
	while(keep_going && read(fd, &event, sizeof(struct input_event)) > 0)
	{
		if (semaphore_wait() >= 0)
		{
			memcpy(&action, shm, sizeof(int));
			action++;
			memcpy(shm, &action, sizeof(int));
			semaphore_release();
		}
	}
}



void monitor_filesys()
{
	int in_instance = inotify_init();

	if (in_instance == -1)
	{
		perror("failed to monitor filesystem");
		return;
	}

	size_t event_size = sizeof(struct inotify_event);
	size_t buf_len = 128 * (event_size+16);
	char buffer[buf_len];
	char* shm = shmat(shared_mem_id, 0, 0);
	char* path = filesys_watch;

	if (path == NULL)
		path = DEFAULT_FILESYS_WATCH;

	printf("monitoring %s\n", path);
	inotify_add_watch(in_instance, path, IN_MODIFY | IN_ACCESS | IN_OPEN);
	int activity;
	
	while(keep_going)
	{
		int read_len = read(in_instance, buffer, buf_len);
		int num_of_events = read_len / event_size;
		
		if (semaphore_wait() >= 0)
		{
			memcpy(&activity, shm, sizeof(int));
			activity += num_of_events;
			memcpy(shm, &activity, sizeof(int));
			semaphore_release();
		}
	}

	close(in_instance);
}


int frequency_to_brightness(float frequency)
{
	/*
	 * This function can be used to calibrate the behaviour of the light.
	 * Looks quite good simply this way.
	 */
	return (int)frequency;
}


void init_resources()
{
	shared_mem_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT|IPC_EXCL|0660);
	if (shared_mem_id == -1)
	{
		exit(EXIT_FAILURE);
	}

	char* shared_mem = shmat(shared_mem_id, 0, 0);
	memset(shared_mem, 0, sizeof(int));
	
	semaphore_id = semget(IPC_PRIVATE, 1, 0600);
	if (semaphore_id < 0)
	{
		exit(EXIT_FAILURE);
	}

	semctl(semaphore_id, 0, SETVAL, 1);
}


void cleanup_resources()
{
	thinklight_set_brightness(0);
	thinklight_uninit();
	semctl(semaphore_id, 0, IPC_RMID);
	shmctl(shared_mem_id, IPC_RMID, 0);
}


void handle_parent_signal(int signal)
{
	printf("process %d: cleaning up, %d\n", getpid(), signal);
	char* shm;
	
	switch (signal)
	{
		case SIGHUP:
		case SIGTERM:
		case SIGQUIT:
		case SIGKILL:
			keep_going = 0;
			shm = shmat(shared_mem_id, 0, 0);
			shmdt(shm);
			cleanup_resources();
			break;
		case SIGCHLD:
			keep_going = 0;
			int status;
			wait(&status);
			shm = shmat(shared_mem_id, 0, 0);
			shmdt(shm);
			cleanup_resources();
			break;
	}
}



void handle_child_signal(int signal)
{
	printf("process %d: cleaning up, %d\n", getpid(), signal);
	
	switch (signal)
	{
		case SIGHUP:
		case SIGTERM:
		case SIGQUIT:
		case SIGKILL:
			keep_going = 0;
			char* shm = shmat(shared_mem_id, 0, 0);
			shmdt(shm);
			break;
	}
}



void read_params(int argc, char **argv)
{
	int option;
	
	while ((option = getopt (argc, argv, "k::m::f")) != -1)
	{
		switch (option)
		{
			case 'k':
				monitor_keyboard_activity = 1;
				break;
			case 'm':
				monitor_mouse_activity = 1;
				break;
			case 'f':
				monitor_filesys_activity = 1;
				filesys_watch = optarg;
				break;
			default:
				abort();
		}
	}
}



int main(int argc, char **argv)
{
	read_params(argc, argv);
	thinklight_init();

	daemonize();
	setuid(UNPRIVILEGED_USER);
	setgid(UNPRIVILEGED_GROUP);
	init_resources();


	if (monitor_keyboard_activity)
	{
		pid_t pid = fork();

		if (pid == 0)
		{
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			signal(SIGHUP, handle_child_signal);
			signal(SIGTERM, handle_child_signal);
			signal(SIGQUIT, handle_child_signal);
			signal(SIGKILL, handle_child_signal);
			count_strokes();
			exit(EXIT_SUCCESS);
		}
		else if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}
	}

	if (monitor_mouse_activity)
	{
		pid_t pid = fork();

		if (pid == 0)
		{
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			signal(SIGHUP, handle_child_signal);
			signal(SIGTERM, handle_child_signal);
			signal(SIGQUIT, handle_child_signal);
			signal(SIGKILL, handle_child_signal);
			monitor_mouse();
			exit(EXIT_SUCCESS);
		}
		else if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}
	}

	if (monitor_filesys_activity)
	{
		pid_t pid = fork();

		if (pid == 0)
		{
			prctl(PR_SET_PDEATHSIG, SIGHUP);
			signal(SIGHUP, handle_child_signal);
			signal(SIGTERM, handle_child_signal);
			signal(SIGQUIT, handle_child_signal);
			signal(SIGKILL, handle_child_signal);
			monitor_filesys();
			exit(EXIT_SUCCESS);
		}
		else if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}
	}
	

	signal(SIGHUP, handle_parent_signal);
	signal(SIGTERM, handle_parent_signal);
	signal(SIGQUIT, handle_parent_signal);
	signal(SIGKILL, handle_parent_signal);
	signal(SIGCHLD, handle_parent_signal);
	control_brightness();

	return 0;
}

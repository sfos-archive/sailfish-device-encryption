/* Copyright (c) 2019 Jolla Ltd. */

#include <dirent.h>
#include <errno.h>
#include <ini.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>

#define USECS(tp) (tp.tv_sec * 1000000L + tp.tv_nsec / 1000L)
#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)

#define TEMPORARY_PASSWORD "guitar"

const char *ask_dir = "/run/systemd/ask-password/";

typedef struct {
	char *ask_file;
	int pid;
	char *socket;
	int accept_cached;
	int echo;
	long not_after;
	char *id;
	char *message;
} ask_info_t;

typedef int (*hide_callback_t)(void *cb_data);

ask_info_t * ask_info_new(char *ask_file)
{
	ask_info_t *ask_info = (ask_info_t *)malloc(sizeof(ask_info_t));
	if (ask_info == NULL)
		return NULL;
	ask_info->ask_file = ask_file;
	ask_info->socket = NULL;
	ask_info->echo = 0;
	ask_info->not_after = -1;
	ask_info->id = NULL;
	ask_info->message = NULL;
	return ask_info;
}

void ask_info_free(ask_info_t *ask_info)
{
	if (ask_info == NULL)
		return;
	free(ask_info->ask_file);
	if (ask_info->socket != NULL)
		free(ask_info->socket);
	if (ask_info->id != NULL)
		free(ask_info->id);
	if (ask_info->message != NULL)
		free(ask_info->message);
	free(ask_info);
}

// If new file is found, allocates ask_info and returns 1,
// if nothing was found, returns 0,
// and in case of an error returns a negative value.
int find_next_ini_file(DIR *dp, ask_info_t **ask_info)
{
	struct dirent *ep;
	char *filename;

	while ((ep = readdir(dp)) != NULL) {
		if (ep->d_type != DT_REG)
			continue;

		if (strncmp(ep->d_name, "ask.", 4) != 0)
			continue;

		filename = (char *)malloc(
				strlen(ask_dir) + strlen(ep->d_name) + 1);
		strcpy(filename, ask_dir);
		strcat(filename, ep->d_name);

		*ask_info = ask_info_new(filename);
		if (*ask_info == NULL) {
			free(filename);
			return -ENOMEM;
		}

		return 1;
	}

	return 0;
}

int time_in_past(long time)
{
	struct timespec tp;

	if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0 && time < USECS(tp))
		return 1;

	return 0;
}

int handle_ini_line(void *user, const char *section, const char *name,
		const char *value)
{
	ask_info_t *ask_info = (ask_info_t *)user;

	if (MATCH("Ask", "PID"))
		ask_info->pid = atoi(value);

	else if (MATCH("Ask", "Socket"))
		ask_info->socket = strdup(value);

	else if (MATCH("Ask", "AcceptCached"))
		ask_info->accept_cached = atoi(value);

	else if (MATCH("Ask", "Echo"))
		ask_info->echo = atoi(value);

	else if (MATCH("Ask", "NotAfter"))
		ask_info->not_after = atol(value);

	else if (MATCH("Ask", "Id"))
		ask_info->id = strdup(value);

	else if (MATCH("Ask", "Message"))
		ask_info->message = strdup(value);

	return 1;
}

// TODO: malloc buf if password is not fixed maximum size (now 30 char)
int send_password(ask_info_t *ask_info, const char *password, int len)
{
	char buf[32];
	int sd, err;
	struct sockaddr_un name;

	if (len >= 0) {
		buf[0] = '+';
		if (password != NULL) {  // Just to be sure
			strncpy(buf+1, password, sizeof(buf)-1);
			buf[sizeof(buf)-1] = '\0';
			len = strlen(buf);
		} else
			len = 1;
	} else {  // Assume cancelled
		buf[0] = '-';
		len = 1;
	}

	if ((sd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
		return -errno;

	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, ask_info->socket, sizeof(name.sun_path));
	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	if (connect(sd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0)
		return -errno;

	if (send(sd, buf, len, 0) < 0)
		return -errno;

	return len;
}

// TODO: Needs to be re-engineered to work with minui event loop stuff,
// but this works for now and represents how I thought it'd work
int get_password(const char *message, int echo, hide_callback_t cb,
			void *cb_data, char **password)
{
	// Temporary implementation, to be replaced
	// message is to be printed, echo tell whether to show password or not
	if (cb(cb_data) == 1)  // Check callback on every "iteration"
		return -1;  // Cancelled

	*password = strdup(TEMPORARY_PASSWORD);
	if (*password == NULL)
		return -ENOMEM;

	return strlen(TEMPORARY_PASSWORD);  // Length of the password given
}

// TODO: May be rewritten to use the event loop system of minui
// Returns 1 if dialog must be hidden, returns 0 otherwise
int hide_dialog(void *cb_data)
{
	ask_info_t *ask_info = (ask_info_t *) cb_data;
	struct stat sb;

	if (time_in_past(ask_info->not_after) == 1)
		return 1;

	if (stat(ask_info->ask_file, &sb) == -1)
		return 1;

	return 0;
}

// FIXME: Might not work well if new files are added to the directory
int main(void)
{
	ask_info_t *ask_info;
	DIR *dp;
	char *password;
	int ret;

	dp = opendir(ask_dir);
	if (dp == NULL)
		return ENOENT;

	while ((ret = find_next_ini_file(dp, &ask_info)) == 1) {
		password = NULL;

		if (ini_parse(ask_info->ask_file, handle_ini_line,
					ask_info) < 0)
			continue;

		if (time_in_past(ask_info->not_after) == 1)
			continue;

		if (kill(ask_info->pid, 0) == ESRCH)
			continue;

		ret = get_password(ask_info->message, ask_info->echo,
					&hide_dialog, ask_info, &password);
		ret = send_password(ask_info, password, ret);

		free(password);
		ask_info_free(ask_info);

		if (ret < 0)
			break;
	}

	closedir(dp);

	if (ret < 0)
		return -ret;

	return 0;
}

/* Copyright (C) 2002 Timo Sirainen */

#include "lib.h"
#include "str.h"
#include "utc-mktime.h"
#include "mbox-from.h"

#include <time.h>
#include <ctype.h>

static const char *weekdays[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int mbox_from_parse(const unsigned char *msg, size_t size,
		    time_t *time_r, char **sender_r)
{
	const unsigned char *msg_start, *sender_end, *msg_end;
	struct tm tm;
	int i, esc, timezone = 0, seen_timezone = FALSE;
	time_t t;

	*time_r = (time_t)-1;
	*sender_r = NULL;

	/* <sender> <date> <moreinfo> */
	msg_start = msg;
	msg_end = msg + size;

	/* get sender */
	if (msg < msg_end && *msg == '"') {
		/* "x y z"@domain - skip the quoted part */
		esc = FALSE;
		msg++;
		while (msg < msg_end && (*msg != '"' || esc)) {
			if (*msg == '\r' || *msg == '\n')
				return -1;
			esc = *msg == '\\';
			msg++;
		}
		msg++;
	} 

	while (msg < msg_end && *msg != ' ') {
		if (*msg == '\r' || *msg == '\n')
			return -1;
		msg++;
	}
	sender_end = msg;
	while (msg < msg_end && *msg == ' ') msg++;

	/* next 24 chars should be in the date in asctime() format, eg.
	   "Thu Nov 29 22:33:52 2001 +0300"

	   - Some put the timezone before the year
	   - Some use a named timezone before or after year, which we ignore
	   - Some don't include seconds
	*/
	if (msg+24 > msg_end)
		return -1;

	memset(&tm, 0, sizeof(tm));

	/* skip weekday */
	msg += 4;

	/* month */
	for (i = 0; i < 12; i++) {
		if (memcasecmp(months[i], msg, 3) == 0) {
			tm.tm_mon = i;
			break;
		}
	}

	if (i == 12 && memcmp(msg, "???", 3) == 0) {
		/* just a hack to parse one special mbox I have :) */
		i = 0;
	}

	if (i == 12 || msg[3] != ' ')
		return -1;
	msg += 4;

	/* day. single digit is usually preceded by extra space */
	if (msg[0] == ' ')
		msg++;
	if (msg[1] == ' ') {
		if (!i_isdigit(msg[0]))
			return -1;
		tm.tm_mday = msg[0]-'0';
		msg += 2;
	} else {
		if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]) || msg[2] != ' ')
			return -1;
		tm.tm_mday = (msg[0]-'0') * 10 + (msg[1]-'0');
		msg += 3;
	}
	if (tm.tm_mday == 0)
		tm.tm_mday = 1;

	/* hour */
	if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]) || msg[2] != ':')
		return -1;
	tm.tm_hour = (msg[0]-'0') * 10 + (msg[1]-'0');
	msg += 3;

	/* minute */
	if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]))
		return -1;
	tm.tm_min = (msg[0]-'0') * 10 + (msg[1]-'0');
	msg += 2;

	/* optional second */
	if (msg[0] == ':') {
		msg++;
		if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]) || msg[2] != ' ')
			return -1;
		tm.tm_sec = (msg[0]-'0') * 10 + (msg[1]-'0');
		msg += 3;
	} else {
		if (msg[0] != ' ')
			return -1;
		msg++;
	}

	/* optional named timezone */
	if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]) ||
	    !i_isdigit(msg[2]) || !i_isdigit(msg[3])) {
		/* skip to next space */
		while (msg < msg_end && *msg != ' ') {
			if (*msg == '\r' || *msg == '\n')
				return -1;
			msg++;
		}
		if (msg+5 > msg_end)
			return -1;
		msg++;
	} else if ((msg[0] == '-' || msg[0] == '+') &&
		   i_isdigit(msg[1]) && i_isdigit(msg[2]) &&
		   i_isdigit(msg[3]) && i_isdigit(msg[4]) && msg[5] == ' ') {
		/* numeric timezone, use it */
                seen_timezone = TRUE;
		timezone = (msg[1]-'0') * 10*60*60 + (msg[2]-'0') * 60*60 +
			(msg[3]-'0') * 10 + (msg[4]-'0');
		if (msg[0] == '-') timezone = -timezone;
		msg += 6;
	}

	/* year */
	if (!i_isdigit(msg[0]) || !i_isdigit(msg[1]) ||
	    !i_isdigit(msg[2]) || !i_isdigit(msg[3]))
		return -1;

	tm.tm_year = (msg[0]-'0') * 1000 + (msg[1]-'0') * 100 +
		(msg[2]-'0') * 10 + (msg[3]-'0') - 1900;
	msg += 4;

	tm.tm_isdst = -1;
	if (!seen_timezone &&
	    msg[0] == ' ' && (msg[1] == '-' || msg[1] == '+') &&
	    i_isdigit(msg[2]) && i_isdigit(msg[3]) &&
	    i_isdigit(msg[4]) && i_isdigit(msg[5])) {
		seen_timezone = TRUE;
		timezone = (msg[2]-'0') * 10*60*60 + (msg[3]-'0') * 60*60 +
			(msg[4]-'0') * 10 + (msg[5]-'0');
		if (msg[1] == '-') timezone = -timezone;
	}

	if (seen_timezone) {
		t = utc_mktime(&tm);
		if (t == (time_t)-1)
			return -1;

		t -= timezone;
		*time_r = t;
	} else {
		/* assume local timezone */
		*time_r = mktime(&tm);
	}

	*sender_r = i_strdup_until(msg_start, sender_end);
	return 0;
}

const char *mbox_from_create(const char *sender, time_t time)
{
	string_t *str;
	struct tm *tm;
	int year;

	str = t_str_new(256);
	str_append(str, "From ");
	str_append(str, sender);
	str_append(str, "  ");

	/* we could use simply asctime(), but i18n etc. may break it.
	   Example: "Thu Nov 29 22:33:52 2001" */
	tm = localtime(&time);

	/* week day */
	str_append(str, weekdays[tm->tm_wday]);
	str_append_c(str, ' ');

	/* month */
	str_append(str, months[tm->tm_mon]);
	str_append_c(str, ' ');

	/* day */
	str_append_c(str, (tm->tm_mday / 10) + '0');
	str_append_c(str, (tm->tm_mday % 10) + '0');
	str_append_c(str, ' ');

	/* hour */
	str_append_c(str, (tm->tm_hour / 10) + '0');
	str_append_c(str, (tm->tm_hour % 10) + '0');
	str_append_c(str, ':');

	/* minute */
	str_append_c(str, (tm->tm_min / 10) + '0');
	str_append_c(str, (tm->tm_min % 10) + '0');
	str_append_c(str, ':');

	/* second */
	str_append_c(str, (tm->tm_sec / 10) + '0');
	str_append_c(str, (tm->tm_sec % 10) + '0');
	str_append_c(str, ' ');

	/* year */
	year = tm->tm_year + 1900;
	str_append_c(str, (year / 1000) + '0');
	str_append_c(str, ((year / 100) % 10) + '0');
	str_append_c(str, ((year / 10) % 10) + '0');
	str_append_c(str, (year % 10) + '0');

	str_append_c(str, '\n');
	return str_c(str);
}

/*
 autostatus - plugin to set status based on the user's location,
 namely, the ip address
 Copyright (C) 2009  Kyle Sun <interskh@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc.

*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#define AUTOSTATUS_ID   "core-autostatus"
#define AUTOSTATUS_NAME	"Auto Status"
#define AUTOSTATUS_VERSION	"0.01"

#include <glib.h>
#include <assert.h>
#include <string.h>

#ifndef G_GNUC_NULL_TERMINATED
# if __GNUC__ >= 4
#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
# else
#  define G_GNUC_NULL_TERMINATED
# endif
#endif

#include <notify.h>
#include <plugin.h>
#include <version.h>
#include <debug.h>
#include <status.h>
#include <savedstatuses.h>
#include <prefs.h>

/* global preference */
static const char * const PREF_NONE = "/plugins/core/autostatus";
static const char * const PREF_LOCATION = "/plugins/core/autostatus/string_location";

PurplePluginPrefFrame* get_plugin_pref_frame(PurplePlugin*);
static PurplePluginUiInfo plugin_prefs = {
	get_plugin_pref_frame,
	0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/********************
 * helper functions *
 ********************/
/* Trace a debugging message. Writes to log file as well as purple
 * debug sink.
 */
void
trace(const char *str, ...)
{
	va_list ap;
	va_start(ap, str);
	char *buf = g_strdup_vprintf(str, ap);
	va_end(ap);

   FILE *log = fopen("/tmp/autostatus.log", "a");
   assert(log);
   time_t t;
   time(&t);
   fprintf(log, "%s: %s\n", ctime(&t), buf);
   fclose(log);

	purple_debug_info(AUTOSTATUS_ID, "%s\n", buf);
	g_free(buf);
}

PurplePluginPrefFrame *get_plugin_pref_frame(PurplePlugin *plugin) {
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;

   trace("creating preferences frame");
   frame = purple_plugin_pref_frame_new();
   pref = purple_plugin_pref_new_with_name_and_label(PREF_LOCATION, "Default location prompt");
   purple_plugin_pref_set_type(pref, PURPLE_PLUGIN_PREF_STRING_FORMAT);

   purple_plugin_pref_frame_add(frame, pref);

   return frame;
}

/* we're adding this here and assigning it in plugin_load because we need
 * a valid plugin handle for our call to purple_notify_message() in the
 * plugin_action_test_cb() callback function */
PurplePlugin *autostatus_plugin = NULL;

/* This function is the callback for the plugin action we added. All we're
 * doing here is displaying a message. When the user selects the plugin
 * action, this function is called. */
static void
plugin_action_test_cb (PurplePluginAction * action)
{
	purple_notify_message (autostatus_plugin, PURPLE_NOTIFY_MSG_INFO,
		"Plugin Actions Test", "This is a plugin actions test :)", NULL, NULL,
		NULL);
}

/* we tell libpurple in the PurplePluginInfo struct to call this function to
 * get a list of plugin actions to use for the plugin.  This function gives
 * libpurple that list of actions. */
static GList *
plugin_actions (PurplePlugin * plugin, gpointer context)
{
	/* some C89 (a.k.a. ANSI C) compilers will warn if any variable declaration
	 * includes an initilization that calls a function.  To avoid that, we
	 * generally initialize our variables first with constant values like NULL
	 * or 0 and assign to them with function calls later */
	GList *list = NULL;
	PurplePluginAction *action = NULL;

	/* The action gets created by specifying a name to show in the UI and a
	 * callback function to call. */
	action = purple_plugin_action_new ("Plugin Action Test", plugin_action_test_cb);

	/* libpurple requires a GList of plugin actions, even if there is only one
	 * action in the list.  We append the action to a GList here. */
	list = g_list_append (list, action);

	/* Once the list is complete, we send it to libpurple. */
	return list;
}

static gboolean
set_status (PurpleAccount *acnt, const char *loc)
{
   // discover the pidgin saved status in use for this account
   const char *savedmessage = "";
   {
      PurpleSavedStatus *savedstatus = purple_savedstatus_get_current();
      if (savedstatus)
      {
         PurpleSavedStatusSub *savedsubstatus = purple_savedstatus_get_substatus(savedstatus, acnt);
         if (savedsubstatus)
         {
            // use account-specific saved status
            savedmessage = purple_savedstatus_substatus_get_message(savedsubstatus);
         }
         else
         {
            // don't have an account-specific saved status, use the general one
            savedmessage = purple_savedstatus_get_message(savedstatus);
         }
      }
   }

   /* generate status */
   char *msg = (char *)malloc(strlen(savedmessage)+strlen(loc)+1);
   strcpy(msg, loc);
   strcat(msg, savedmessage);

	PurpleStatus *status = purple_account_get_active_status (acnt);
   GList *attrs = NULL;
   attrs = g_list_append(attrs, "message");
   attrs = g_list_append(attrs, (gpointer)msg);
   purple_status_set_active_with_attrs_list(status, TRUE, attrs);
   g_list_free(attrs);
   free(msg);

   trace("setup the account(%s) status: %s", acnt->username, msg);

   return TRUE;
}

static gboolean
plugin_load (PurplePlugin * plugin)
{

   trace("plugin loading");

   /* TODO time out */
	//g_tid = purple_timeout_add(INTERVAL, &cb_timeout, 0);

	autostatus_plugin = plugin; /* assign this here so we have a valid handle later */

   GList *acnt = NULL, *head = NULL;
//   const char *loc= "@INI | ";
   char *loc = purple_prefs_get_string(PREF_LOCATION);

   head = acnt = purple_accounts_get_all_active();

   while (acnt != NULL) {
      if ((PurpleAccount *)acnt->data != NULL) {
         set_status (acnt->data, loc);
      }
      acnt = acnt->next;
   }

   if (head != NULL)
      g_list_free(head);

   trace("status set for all accounts");

	return TRUE;
}

/* For specific notes on the meanings of each of these members, consult the C Plugin Howto
 * on the website. */
static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,

   AUTOSTATUS_ID,
	AUTOSTATUS_NAME,
	AUTOSTATUS_VERSION, 

	"Auto Status Plugin",
	"Auto Status Plugin",
	"Kyle Sun <interskh@gmail.com>", /* correct author */
	"http://xxx.xxx",


	plugin_load,
	NULL,
	NULL,

	NULL,
	NULL,
	&plugin_prefs,
	plugin_actions,		/* this tells libpurple the address of the function to call
				   to get the list of plugin actions. */
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin (PurplePlugin * plugin)
{
   trace("init");
	purple_prefs_add_none(PREF_NONE);
	purple_prefs_add_string(PREF_LOCATION, "");
   trace("done");
}

PURPLE_INIT_PLUGIN (hello_world, init_plugin, info)

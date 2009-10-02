/*
 autostatus - plugin to set status based on the user's ip address

 Copyright (c) 2009, Kyle Sun <interskh@gmail.com>
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
#include <network.h>

typedef struct Rule{
   gchar *status;
   guint32 ip;
   guint32 netmask;
}Rule;

/* global preference */
static const char * const PREF_NONE = "/plugins/core/autostatus";
static const char * const PREF_CONFIG = "/plugins/core/autostatus/string_config";

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

Rule *rules = NULL;
guint32 rule_cnt = 0;
guint32 myip = 0;
gchar *str_myip = NULL;
gchar *loc = "";


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

/* XXX be careful */
gboolean
load_one_rule(char **ptr, Rule *rule)
{
   char *ch = *ptr;
   /*check "xxx.xxx.xxx.xxx/xxx\n" */
   unsigned int i = 0;
   unsigned int ip[4] = {0};
   unsigned int nm = 0;
   int ip_index = 0;
   gboolean n_ip = TRUE; /* next char should be ip address */
   gboolean n_num = TRUE; /* next char should be a number */

   /* read first line */
   while (ch[i]!=0 && ch[i]!='\n') {
      switch (ch[i]) {
         case '.': 
            if (n_num == TRUE) return FALSE;
            if (ip_index>3) return FALSE;
            if (ip[ip_index] > 255) return FALSE;
            ip_index++;
            n_num = TRUE;
            break;
         case '/':
            if (ip_index!=3 || n_num == TRUE) return FALSE;
            n_num = TRUE; n_ip = FALSE;
            break;
         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
            n_num = FALSE;
            if (!n_ip) {
               nm = nm *10 + (ch[i] - '0');
            }
            else {
               ip[ip_index] = ip[ip_index]*10 + (ch[i] - '0');
            }
            break;
         case '\r':
            break;
         default:
            return FALSE;
      }
      i++;
   }
   if (ch[i] == 0) return FALSE;
   else i++;
   if (n_num == TRUE || n_ip == TRUE) return FALSE;

   /* read second line */
   char *str = ch + i;
   while (ch[i]!=0 && ch[i]!='\n') {
      i++;
   }
   if (ch[i] == 0) return FALSE;

   /* construct rule */
   rule->ip = (ip[0]<<24) + (ip[1]<<16) + (ip[2]<<8) + ip[3];
   rule->netmask = nm;
   char *status = (char *)malloc(ch+i+1-str);
   strncpy(status, str, ch+i-str);
   status[ch+i-str] = '\0';
   rule->status = status;

   *ptr = ch+i+1;

   trace("get one rule:\nip: %d.%d.%d.%d/%d\nstr: %s", \
         ip[0], ip[1], ip[2], ip[3], nm, status);

   return TRUE;
}

/* On-disk configuration file format:
 * ip(xxx.xxx.xxx.xxx)/netmask
 * status
 * ip(xxx.xxx.xxx.xxx)/netmask
 * status
 */
gboolean
load_config()
{
   /* load configuration file */
   gchar *file = purple_prefs_get_string(PREF_CONFIG);
   gchar *buf;
   gsize len;
   if (!g_file_get_contents(file, &buf, &len, NULL)) {
      trace ("Load config file failed");
      return FALSE;
   }

   /* count rule number */
   gsize i;
   for (i=0; i<len; i++) {
      if (buf[i] == '\n') rule_cnt++;
   }
   /* count here is not exactly the true, because it may happen that 
    * \n\n\n, this would be fixed later */
   rule_cnt = (rule_cnt +1)/2;   

   rules = (Rule *)malloc(rule_cnt * sizeof(Rule));

   /* parse rules */
   char *ch = buf;
   rule_cnt = 0;
   while (load_one_rule(&ch, rules+rule_cnt)) {
      rule_cnt++;
   }

   trace("get number of rules: %d", rule_cnt);

   return TRUE;
}

/* pre: ch should be well formatted */
guint32 ip_atoi(const char *ch) {
   int i=0, j=0;
   unsigned int ip[4]={0};
   while (ch[i] != '\0') {
      if (ch[i] == '.') j++;
      else ip[j] = (ch[i]-'0') + 10*ip[j];
      i++;
   }
   assert(j==3);
   for (i=0; i<4; i++) {
      assert(ip[i]<256);
   }

   return ((ip[0]<<24) + (ip[1]<<16) + (ip[2]<<8) + ip[3]);
}

PurplePluginPrefFrame *get_plugin_pref_frame(PurplePlugin *plugin) {
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;

   frame = purple_plugin_pref_frame_new();

   pref = purple_plugin_pref_new_with_name_and_label(PREF_CONFIG, "Configuration file location: ");
   purple_plugin_pref_frame_add(frame, pref);
   
   pref = purple_plugin_pref_new_with_label("Current IP address:");
   purple_plugin_pref_set_type(pref,PURPLE_PLUGIN_PREF_NONE);
   purple_plugin_pref_frame_add(frame, pref);
   pref = purple_plugin_pref_new_with_label(str_myip);
   purple_plugin_pref_set_type(pref,PURPLE_PLUGIN_PREF_INFO);
   purple_plugin_pref_frame_add(frame, pref);

   pref = purple_plugin_pref_new_with_label("Current rule:");
   purple_plugin_pref_set_type(pref,PURPLE_PLUGIN_PREF_NONE);
   purple_plugin_pref_frame_add(frame, pref);
   pref = purple_plugin_pref_new_with_label(loc);
   purple_plugin_pref_set_type(pref,PURPLE_PLUGIN_PREF_INFO);
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
   char *msg;
   if (savedmessage) {
      msg = (char *)malloc(strlen(savedmessage)+strlen(loc)+1);
      strcpy(msg, loc);
      strcat(msg, savedmessage);
   }
   else msg = loc;

	PurpleStatus *status = purple_account_get_active_status (acnt);
   GList *attrs = NULL;
   attrs = g_list_append(attrs, "message");
   attrs = g_list_append(attrs, (gpointer)msg);
   purple_status_set_active_with_attrs_list(status, TRUE, attrs);
   g_list_free(attrs);
   trace("setup the account(%s) status: %s", acnt->username, msg);

   if (savedmessage) {
      free(msg);
   }

   return TRUE;
}

static gboolean
set_status_all()
{
   trace("status setting..");

   char *ip = purple_network_get_my_ip(-1);
   if (strcmp(str_myip, ip) != 0) {
      strcpy(str_myip, ip);
      myip = ip_atoi(ip);

      /* find the longest prefix rule */
      guint32 i=0;
      guint32 max_len = 0;
      guint32 fit = 0;
      gboolean found = FALSE;
      for (i=0; i<rule_cnt; i++){
         if ((rules[i].netmask > max_len) && \
               (((myip ^ rules[i].ip) & (~0 << (32-rules[i].netmask))) == 0)) {
            max_len = rules[i].netmask;
            fit = i;
            found = TRUE;
         }
      }
      if (found) {
         loc = rules[fit].status;
         trace("Found fit rules, %s", loc);
      }
      else loc = "";
   }

   GList *acnt = NULL, *head = NULL;
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

static gboolean
plugin_load (PurplePlugin * plugin)
{

   trace("plugin loading");

	autostatus_plugin = plugin; /* assign this here so we have a valid handle later */

   str_myip = (gchar *)malloc(strlen("xxx.xxx.xxx.xxx"));

   /* TODO time out */
   /* Note: here need to consider serverl situation:
    * 1. enable/disable account
    * 2. change status
    * 3. how to make sure nothing changes
    * 4. diffenent status for different accounts
    * 5. move to another place!
    */
   /* refresh each 10 seconds */
	guint g_tid = purple_timeout_add_seconds(10, set_status_all, 0);

   load_config();

   if (set_status_all()) trace ("plugin succesfully loaded");

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
	"http://",


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
   gchar *dir = g_build_filename(purple_user_dir(), "autostatus.config", NULL);

	purple_prefs_add_none(PREF_NONE);
	purple_prefs_add_string(PREF_CONFIG, dir);
}

PURPLE_INIT_PLUGIN (hello_world, init_plugin, info)

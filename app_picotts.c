/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 Ramon Martinez 
 *
 * Ramon Martinez <rampa@encomix.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the COPYING file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Say text to the user, using PicoTTS TTS engine.
 *
 * \author\verbatim Ramon Martinez <rampa@encomix.org>  based on works of Lefteris Zafiris <zaf.000@gmail.com>\endverbatim
 *
 * \extref PicoTTS text to speech Synthesis System 
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 00 $")
#include <stdio.h>
#include <string.h>
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"

#define AST_MODULE "PicoTTS"
#define FLITE_CONFIG "picotts.conf"
#define MAXLEN 2048
#define DEF_RATE 8000
#define DEF_VOICE "es-ES"
#define DEF_DIR "/tmp"


static char *app = "PicoTTS";
static char *synopsis = "Say text to the user, using PicoTTS TTS engine";
static char *descrip =
	" PicoTTS(text[,intkeys]): This will invoke the PicoTTS TTS engine, send a text string,\n"
	"get back the resulting waveform and play it to the user, allowing any given interrupt\n"
	"keys to immediately terminate and return the value, or 'any' to allow any number back.\n";

static int target_sample_rate;
static int usecache;
static const char *cachedir;
static const char *voice_name;
static struct ast_config *cfg;
static struct ast_flags config_flags =  { 0 };

static int read_config(void)
{
	const char *temp;
	/* set default values */
	target_sample_rate = DEF_RATE;
	usecache = 0;
	cachedir = DEF_DIR;
	voice_name = DEF_VOICE;

	cfg = ast_config_load(FLITE_CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING,
				"PicoTTS: Unable to read config file %s. Using default settings\n",
				FLITE_CONFIG);
	} else {
		if ((temp = ast_variable_retrieve(cfg, "general", "usecache")))
			usecache = ast_true(temp);

		if ((temp = ast_variable_retrieve(cfg, "general", "cachedir")))
			cachedir = temp;

		if ((temp = ast_variable_retrieve(cfg, "general", "voice")))
			voice_name = temp;

		if ((temp = ast_variable_retrieve(cfg, "general", "samplerate")))
			target_sample_rate = atoi(temp);
	}

	if (target_sample_rate != 8000 && target_sample_rate != 16000) {
		ast_log(LOG_WARNING, "PicoTTS: Unsupported sample rate: %d. Falling back to %d\n",
				target_sample_rate, DEF_RATE);
		target_sample_rate = DEF_RATE;
	}
	return 0;
}


static int picotts_text_to_wave( const char *filedata, const char *language , const char *texttospeech)
{
 int res_tts=0;
 char temp[2048];
 sprintf(temp, "pico2wave -w %s -l %s '%s'", filedata, language,texttospeech);

                 
 res_tts=system(temp);
 ast_log(LOG_WARNING, "PicoTTS: command %s, code %d.\n",temp,res_tts);

 return (0);
}


static int delete_wave( const char *filedata)
{
 return unlink(filedata);
}




static int picotts_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *mydata;
	int writecache = 0;
	char MD5_name[33] = "";
	int sample_rate;
	char cachefile[MAXLEN] = "";
	char tmp_name[20];
	char raw_tmp_name[26];
	char rawpico_tmp_name[26];	

	int voice;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(text);
		AST_APP_ARG(lang);
		AST_APP_ARG(interrupt);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "PicoTTS requires an argument (text)\n");
		return -1;
	}

	mydata = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, mydata);


	if (args.interrupt && !strcasecmp(args.interrupt, "any"))
		args.interrupt = AST_DIGIT_ANY;

	args.text = ast_strip_quoted(args.text, "\"", "\"");
	args.lang = ast_strip_quoted(args.lang, "\"", "\"");
	if (ast_strlen_zero(args.text)) {
		ast_log(LOG_WARNING, "PicoTTS: No text passed for synthesis.\n");
		return res;
	}

	if (ast_strlen_zero(args.lang)) {
		ast_log(LOG_WARNING, "PicoTTS: language is default: %s.\n", voice_name);
		return res;
	}
	else
	{
	 voice_name=args.lang;
	}

	ast_debug(1, "PicoTTS:\nText passed: %s\nInterrupt key(s): %s\nVoice: %s\nRate: %d\n",
			args.text, args.interrupt, voice_name, target_sample_rate);

	/*Cache mechanism */
	if (usecache) {
		ast_md5_hash(MD5_name, args.text);
		if (strlen(cachedir) + strlen(MD5_name) + 6 <= MAXLEN) {
			ast_debug(1, "PicoTTS: Activating cache mechanism...\n");
			snprintf(cachefile, sizeof(cachefile), "%s/%s", cachedir, MD5_name);
			if (ast_fileexists(cachefile, NULL, NULL) <= 0) {
				ast_debug(1, "PicoTTS: Cache file does not yet exist.\n");
				writecache = 1;
			} else {
				ast_debug(1, "PicoTTS: Cache file exists.\n");
				if (chan->_state != AST_STATE_UP)
					ast_answer(chan);
				res = ast_streamfile(chan, cachefile, chan->language);
				if (res) {
					ast_log(LOG_ERROR, "PicoTTS: ast_streamfile from cache failed on %s\n",
							chan->name);
				} else {
					res = ast_waitstream(chan, args.interrupt);
					ast_stopstream(chan);
					return res;
				}
			}
		}
	}

	/* Create temp filenames */
	snprintf(tmp_name, sizeof(tmp_name), "/tmp/picotts_%li", ast_random() %99999999);
	if (target_sample_rate == 8000)
		snprintf(raw_tmp_name, sizeof(raw_tmp_name), "%s.wav", tmp_name);
	if (target_sample_rate == 16000)
	{
		snprintf(raw_tmp_name, sizeof(raw_tmp_name), "%s.gsm", tmp_name);
		snprintf(rawpico_tmp_name, sizeof(raw_tmp_name), "%s.wav", tmp_name);
	}		

	/* Invoke PicoTTS */
	//picotts_init();
	if (strcmp(voice_name, "en-US") == 0 )
		voice = 1;
	else if (strcmp(voice_name, "en-GB") == 0)
		voice = 2;
	else if (strcmp(voice_name, "de-DE") == 0)
		voice = 3;
	else if (strcmp(voice_name, "es-ES") == 0)
		voice = 4;
	else if (strcmp(voice_name, "fr-FR") == 0)
		voice = 5;
	else if (strcmp(voice_name, "it-IT") == 0)
                voice = 6;
	else {
		ast_log(LOG_WARNING, "PicoTTS: Unsupported voice %s. Using default voice.\n",
				voice_name);
		voice = 1;
		voice_name = "en-US";
	}

	res = picotts_text_to_wave(rawpico_tmp_name,voice_name,args.text);
	
	char temp[1024];
	sprintf(temp,"sox %s -r 8000 -c1 %s resample -ql",rawpico_tmp_name,raw_tmp_name);
	system(temp);
	unlink(rawpico_tmp_name);
	sample_rate = 16000;


	if (res==-1) {
		ast_log(LOG_ERROR, "PicoTTS: failed to write file %s , code %d\n", raw_tmp_name,res);
		return res;
	}


	if (writecache) {
		ast_debug(1, "PicoTTS: Saving cache file %s\n", cachefile);
		ast_filecopy(tmp_name, cachefile, NULL);
	}

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	res = ast_streamfile(chan, tmp_name, chan->language);
	if (res) {
		ast_log(LOG_ERROR, "PicoTTS: ast_streamfile failed on %s\n", chan->name);
	} else {
		res = ast_waitstream(chan, args.interrupt);
		ast_stopstream(chan);
	}

	ast_filedelete(tmp_name, NULL);
	return res;
}

static int reload(void)
{
	ast_config_destroy(cfg);
	read_config();
	return 0;
}

static int unload_module(void)
{
	ast_config_destroy(cfg);
	return ast_unregister_application(app);
}

static int load_module(void)
{
	read_config();
	return ast_register_application(app, picotts_exec, synopsis, descrip) ?
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "PicoTTS TTS Interface",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
			);


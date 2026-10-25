#include "harness.h"
/* Audio Setup's two rows and Sound Processing's one, each a spinner of a
setting in config.toml; a screen's widgets are made again each time it opens
(ui_widget.c deletes the screen a page opens in place of) */
#define MAXIMUM_STRINGS 64
struct widget_instance {
 long definition_tag_index; short type;
 struct widget_instance *parent,*child,*next;
 union { struct { short selected_index,number_of_items; } list; } parameters;
};
static int _stricmp(char const *a,char const *b){return strcasecmp(a,b);}
#define platform_log(...) ((void)0)
static boolean going_back;
#define ui_widget_port_going_back() going_back
#include "under_test_setting.inc"
static char const *names[]={"audio.volume","audio.music_volume","audio.reverb"};
static char config[3][16]={"1.0","0.5","true"};
static int writes;
static struct pc_menu_setting settings[3];
static struct pc_menu_setting *pc_menu_setting_get(long definition_index)
{return definition_index>=0&&definition_index<3?&settings[definition_index]:NULL;}
static int setting_index(char const *name){for(int i=0;i<3;i++)if(!strcmp(names[i],name))return i;return -1;}
static boolean setting_text(char const *name,char *text,unsigned int size,boolean default_value)
{int i=setting_index(name);(void)default_value;if(i<0)return FALSE;snprintf(text,size,"%s",config[i]);return TRUE;}
static boolean setting_write(char const *name,char const *value)
{int i=setting_index(name);if(i<0)return FALSE;snprintf(config[i],sizeof(config[i]),"%s",value);writes++;return TRUE;}
#include "under_test.inc"
static struct widget_instance screen,row[2],spinner[2],item[2][3];
/* a screen of the spinners [first, first + count): made, and each loaded as its created event does */
static void screen_open(int first,int count)
{
 memset(&screen,0,sizeof(screen));screen.child=&row[0];
 for(int i=0;i<count;i++){
  row[i]=(struct widget_instance){.parent=&screen,.child=&spinner[i],.next=i<count-1?&row[i+1]:NULL,.definition_tag_index=100+i};
  spinner[i]=(struct widget_instance){.type=2,.parent=&row[i],.child=&item[i][0],.definition_tag_index=first+i};
  spinner[i].parameters.list.number_of_items=(short)settings[first+i].value_count;
  for(int j=0;j<3;j++)item[i][j]=(struct widget_instance){.type=2,.parent=&spinner[i],.next=j<2?&item[i][j+1]:NULL,
   .definition_tag_index=first+i};
  CHECK(setting_load(&spinner[i]),"load %d",first+i);
  for(int j=0;j<3;j++)CHECK(setting_load(&item[i][j]),"item load");
 }
}
static void audio_open(void){screen_open(0,2);}
static void audio_back(void){going_back=TRUE;audio_open();going_back=FALSE;}
static void sound_processing_open(void){screen_open(2,1);}
/* the row that opens Sound Processing: run="port settings keep" open=... */
static void sound_processing_row(void){settings_each(&screen,setting_keep);sound_processing_open();}
static void ok(void){settings_each(&screen,setting_changed_save);}
static short shown(int i){return spinner[i].parameters.list.selected_index;}
static void setup(void)
{
 static char const *volumes[]={"0","0.5","1.0"},*switches[]={"true","false"};
 for(int i=0;i<3;i++){
  settings[i]=(struct pc_menu_setting){.definition_index=i,.setting=names[i],.loaded_index=NONE,.kept_index=NONE};
  settings[i].value_count=i<2?3:2;
  for(int j=0;j<settings[i].value_count;j++)settings[i].values[j]=i<2?volumes[j]:switches[j];
 }
}
/* Audio Setup's MASTER changed to 0, then SOUND PROCESSING opened and REVERB changed */
static void round_trip(boolean sub_ok)
{
 audio_open();CHECK(shown(0)==2&&shown(1)==1,"saved values shown");
 spinner[0].parameters.list.selected_index=0;
 sound_processing_row();CHECK(shown(0)==0,"reverb shown %d",shown(0));
 spinner[0].parameters.list.selected_index=1;
 if(sub_ok)ok();
 audio_back();
}
int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";setup();
 CASE("kept") {
  round_trip(TRUE);
  CHECK(shown(0)==0,"MASTER's change lost on the way back: shows %s",settings[0].values[shown(0)]);
  CHECK(shown(1)==1,"MUSIC changed");
  CHECK(!strcmp(config[0],"1.0"),"MASTER saved before OK: %s",config[0]);
  CHECK(!strcmp(config[2],"false"),"Sound Processing's OK did not save REVERB");
 } else CASE("ok") {
  round_trip(TRUE);writes=0;ok();
  CHECK(!strcmp(config[0],"0"),"OK did not save MASTER: %s",config[0]);
  CHECK(writes==1,"OK wrote %d settings",writes);
  audio_open();CHECK(shown(0)==0,"reopened shows %s",settings[0].values[shown(0)]);
  writes=0;ok();CHECK(writes==0,"second OK wrote %d",writes);
 } else CASE("cancel") {
  round_trip(TRUE);
  /* Cancel and B go back: the screen is deleted, nothing saved */
  CHECK(!strcmp(config[0],"1.0"),"Cancel saved MASTER: %s",config[0]);
  audio_open();CHECK(shown(0)==2,"Cancel's change shown again: %s",settings[0].values[shown(0)]);
  writes=0;ok();CHECK(writes==0,"OK after Cancel wrote %d",writes);
 } else CASE("page_cancel") {
  round_trip(FALSE);
  CHECK(!strcmp(config[2],"true"),"Sound Processing's Cancel saved REVERB");
  CHECK(shown(0)==0,"Sound Processing's Cancel dropped MASTER's change");
  ok();CHECK(!strcmp(config[0],"0"),"OK did not save MASTER");
 } else CASE("unchanged") {
  audio_open();sound_processing_row();
  CHECK(settings[0].kept_index==NONE&&settings[1].kept_index==NONE,"unchanged rows kept");
  /* saved elsewhere meanwhile: the screen shows config.toml's */
  snprintf(config[1],sizeof(config[1]),"0");
  audio_back();CHECK(shown(0)==2&&shown(1)==0,"shows %d %d",shown(0),shown(1));
 } else CASE("closed") {
  /* the menus closed on Sound Processing (the pause button in a game): Audio
  Setup opened afresh shows config.toml's, and so does going back to it later
  (the pause button leaves the history) */
  audio_open();spinner[0].parameters.list.selected_index=0;sound_processing_row();
  audio_open();CHECK(shown(0)==2,"unsaved MASTER shown on a fresh Audio Setup: %s",settings[0].values[shown(0)]);
  audio_back();CHECK(shown(0)==2,"unsaved MASTER came back: %s",settings[0].values[shown(0)]);
  writes=0;ok();CHECK(writes==0,"OK wrote %d",writes);
 } else return 2;
 return 0;
}

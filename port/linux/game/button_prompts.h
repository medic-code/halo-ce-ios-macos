/*
BUTTON_PROMPTS.H

The keys that the button prompts name for the player on the keyboard and
mouse (button_prompts.c).
*/

#ifndef __BUTTON_PROMPTS_H
#define __BUTTON_PROMPTS_H

/* the text that a button icon of a prompt ("Hold %action to pick up") is
drawn as for a local player: the key that does what the icon names, or
NULL, the controller's icon. The text lasts until the next call. */
wchar_t const *button_prompt_text(
	short local_player_index,
	short icon_type);

#endif

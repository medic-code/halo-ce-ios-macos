/*
BUTTON_PROMPTS.H
*/

#ifndef __BUTTON_PROMPTS_H
#define __BUTTON_PROMPTS_H

/* NULL to draw the controller's icon; the text lasts until the next call */
wchar_t const *button_prompt_text(
	short local_player_index,
	short icon_type);

void button_prompt_scores_key(
	short local_player_index,
	wchar_t *text,
	long size);

#endif

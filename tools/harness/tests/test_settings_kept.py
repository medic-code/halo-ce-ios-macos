"""A settings screen's changes kept while one of its pages is open, saved by its OK and dropped by its Cancel."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import port_settings  # noqa: E402
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ['kept', 'ok', 'cancel', 'page_cancel', 'unchanged', 'closed']


def generated(fault=None):
    source = read('port/linux/game/menu_functions.c')
    code = '\n'.join(function(source, name) for name in
                     ['text_is_number', 'spinner_item', 'setting_value_index', 'setting_load', 'settings_each',
                      'setting_changed_save', 'setting_keep'])
    if fault:
        code = mutated(code, *fault)
    return (('under_test_setting.inc', structure(source, 'pc_menu_setting')), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('settings_kept', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('\t\twidget->parameters.list.selected_index = setting->kept_index;\n', '\n'), 'kept'),
    (('\tsetting->kept_index = NONE;\n', '\n'), 'closed'),
    (('widget->parameters.list.selected_index = setting->kept_index;',
      'widget->parameters.list.selected_index = setting->loaded_index = setting->kept_index;'), 'ok'),
    (('index != setting->loaded_index ? index : NONE', 'index != setting->loaded_index ? setting->loaded_index : NONE'),
     'kept'),
    (('ui_widget_port_going_back())', '(ui_widget_port_going_back() || TRUE))'), 'closed'),
])
def test_negative_control(fault, case):
    status, output = run(build('settings_kept', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_the_rows_that_open_pages_keep_the_screens_changes():
    pages = 0
    for name, lines in port_settings.settings_files().items():
        for row in re.findall(r'<widget name="[^"]*/op_[^"]*"[^>]*>(?:\n\t\t[^\n]*)*', '\n'.join(lines)):
            for event in ('a', 'start'):
                opens = re.search(rf'<on event="{event}"([^>]*) open="', row)
                if opens:
                    pages += 1
                    assert 'run="port settings keep"' in opens.group(1), (name, row.split('\n')[0])
    assert pages == 2 * 10

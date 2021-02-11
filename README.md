# wpadremap
Simple Wayland client which translates drawing tablet buttons to virtual keypresses.

Button mapping is hard coded, buttons `0`, `...`, `9` get mapped to `Alt+0`, `...`, `Alt+9`. Other buttons are not mapped,
see the `pad_button_to_keycode` function if you want different mappings.

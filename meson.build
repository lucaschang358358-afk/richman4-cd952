project('richman4-cd952', 'c',
  version : '5.0.0',
  default_options : ['warning_level=2'])

c = meson.get_compiler('c')
mutekix_lib = c.find_library('mutekix', required : true)
elf2bestape = find_program('elf2bestape', required: false)
romtool = find_program('romtool', required: false)
py3 = find_program('python3', required: true)

touch_adapt_h = custom_target('muteki-touch-adapt',
  input: 'gen_touch_header.py',
  output: 'muteki_touch_adapt.h',
  command: [py3, '@INPUT@', '@OUTPUT@'])

game_elf = executable('richman4-cd952',
  [touch_adapt_h, 'maps.c', 'gfx.c', 'game.c', 'platform_muteki.c'],
  name_suffix: 'elf',
  dependencies: mutekix_lib,
  install : false,
  c_args: ['-O3'],
  link_args: ['-Wl,--gc-sections'])

if romtool.found() and elf2bestape.found()
  romspec = custom_target('romspec',
    input: 'romspec.toml',
    output: 'romspec.rom',
    command: [romtool, 'build', '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true)

  custom_target('richman4-bestape',
    input: [game_elf, romspec],
    output: 'richman4-cd952.exe',
    command: [elf2bestape, '-o', '@OUTPUT@', '-r', '@INPUT1@', '@INPUT0@'],
    build_by_default: true)
endif

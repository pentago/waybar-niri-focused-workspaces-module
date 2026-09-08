project(
    'waybar-niri-focused-workspaces',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    default_options: ['cpp_std=c++20', 'warning_level=2'],
)

gtk = dependency('gtk+-3.0', version: '>=3.22.0')
jsoncpp = dependency('jsoncpp')
fmt = dependency('fmt')

module = shared_library(
    'niri-focused-workspaces',
    ['src/module.cpp', 'src/niri_ipc.cpp', 'src/workspaces.cpp'],
    include_directories: include_directories('include'),
    dependencies: [gtk, jsoncpp, fmt],
    name_prefix: '',
    install: true,
    install_dir: get_option('libdir') / 'waybar',
)

test(
    'workspaces',
    executable(
        'workspaces_test',
        ['test/workspaces_test.cpp', 'src/workspaces.cpp'],
        include_directories: include_directories('src'),
        dependencies: [jsoncpp],
        cpp_args: ['-UNDEBUG'],
    ),
)

scdoc = find_program('scdoc', required: get_option('man-pages'))
if scdoc.found()
    custom_target(
        'waybar-niri-focused-workspaces.5',
        input: 'man/waybar-niri-focused-workspaces.5.scd',
        output: 'waybar-niri-focused-workspaces.5',
        command: [scdoc],
        feed: true,
        capture: true,
        install: true,
        install_dir: get_option('mandir') / 'man5',
    )
endif

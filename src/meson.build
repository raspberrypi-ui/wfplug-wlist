gtkmm = dependency('gtkmm-3.0', version: '>=3.24')
wfpanel = dependency('wf-panel-pi')
menu_cache = dependency('libmenu-cache')

wsources = files(
  'tlist.c',
  'tlist.cpp'
)

wdeps = [ deps, gtkmm, wfpanel, menu_cache ]

wargs = [ '-DPACKAGE_DATA_DIR="' + wresource_dir + '"', '-DGETTEXT_PACKAGE="wfplug_' + meson.project_name() +'"' ]

shared_module('lib' + meson.project_name(), wsources,
        dependencies: wdeps,
        install: true,
        install_dir: get_option('libdir') / 'wf-panel-pi',
        c_args : wargs,
        cpp_args : wargs,
        name_prefix: ''
)

metadata = files(
  'tlist.xml'
)
install_data(metadata, install_dir: metadata_dir)

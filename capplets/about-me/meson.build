desktop = 'mate-about-me.desktop'

i18n.merge_file(
  type: 'desktop',
  input: desktop + '.in',
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: mcc_desktopdir
)

sources = files(
  'e-image-chooser.c',
  'mate-about-me.c',
  'mate-about-me-fingerprint.c',
  'mate-about-me-password.c'
)

sources += gnome.compile_resources(
  'mate-about-me-resources',
  'org.mate.mcc.am.gresource.xml',
  c_name: 'about_me',
  export: true
)

cflags += [
  '-DDATADIR="@0@"'.format(mcc_datadir),
  '-DMATECC_DATA_DIR="@0@"'.format(mcc_pkgdatadir),
  '-DMATECC_PIXMAP_DIR="@0@"'.format(mcc_pixmaps),
  '-DMATECC_FACES_DIR="@0@"'.format(join_paths(mcc_pkgdatadir, 'faces')),
  '-DMATECC_SOURCE_FACES_DIR="@0@"'.format(join_paths(meson.project_source_root(), 'data', 'faces')),
  '-DMATELOCALEDIR="@0@"'.format(mcc_localedir),
]

faces_dir = join_paths(meson.project_source_root(), 'data', 'faces')
face_files = []
face_file_list = run_command(
  'find',
  faces_dir,
  '-maxdepth', '1',
  '-type', 'f',
  check: true
).stdout().strip().split('\n')

foreach face_file: face_file_list
  if face_file.endswith('.jpg') or face_file.endswith('.png') or face_file.endswith('.svg')
    face_files += files(face_file)
  endif
endforeach

install_data(
  face_files,
  install_dir : join_paths(mcc_pkgdatadir, 'faces')
)

executable(
  'mate-about-me',
  sources : sources,
  include_directories: config_inc,
  dependencies : [common_deps, libcommon_dep, gstreamer_dep, gstreamer_app_dep, gstreamer_video_dep],
  c_args : cflags,
  install : true,
  install_dir : get_option('bindir')
)
